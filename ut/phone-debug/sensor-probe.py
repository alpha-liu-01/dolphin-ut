#!/usr/bin/env python3
# Stage 6.6 probe: reads sensorfwd's accelerometer and gyroscope the way libsensorclient does
# (D-Bus: loadPlugin, requestSensor; socket /run/sensord.sock: session id, tag byte, then batches of
# [uint32 count][count samples]), and prints the sample size and values as float and as int.
import dbus, os, select, socket, struct, sys, time

bus = dbus.SystemBus()
SERVICE = 'com.nokia.SensorService'
mgr = dbus.Interface(bus.get_object(SERVICE, '/SensorManager'), 'local.SensorManager')
seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 2.0
for name, iface in (('accelerometersensor', 'local.AccelerometerSensor'),
                    ('gyroscopesensor', 'local.GyroscopeSensor')):
    print('==', name, 'loadPlugin', bool(mgr.loadPlugin(name)))
    sid = int(mgr.requestSensor(name, dbus.Int64(os.getpid())))
    print('session', sid)
    if sid < 0:
        continue
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.connect('/run/sensord.sock')
    sock.sendall(struct.pack('i', sid))
    print('tag', sock.recv(1))
    obj = bus.get_object(SERVICE, '/SensorManager/' + name)
    if name == 'accelerometersensor':
        xml = obj.Introspect(dbus_interface='org.freedesktop.DBus.Introspectable')
        print('methods:', sorted(set(l.split('"')[1] for l in xml.splitlines() if '<method name=' in l)))
    sensor = dbus.Interface(obj, iface)
    try:
        sensor.setStandbyOverride(sid, True)
    except Exception as e:
        print('setStandbyOverride:', e)
    try:
        sensor.setInterval(sid, 10)
    except Exception as e:
        print('setInterval:', e)
    sensor.start(sid)
    data = b''
    end = time.time() + seconds
    while time.time() < end:
        r, _, _ = select.select([sock], [], [], 0.2)
        if r:
            data += sock.recv(65536)
    sensor.stop(sid)
    mgr.releaseSensor(name, sid, dbus.Int64(os.getpid()))
    sock.close()
    # Parse batches.
    pos, samples, batches, size = 0, [], 0, None
    while pos + 4 <= len(data):
        count = struct.unpack_from('I', data, pos)[0]
        if count == 0 or count > 1000:
            print('odd count', count, 'at', pos); break
        if size is None:
            # Guess the sample size from the next count, if any.
            for guess in (24, 20, 32, 16):
                nxt = pos + 4 + count * guess
                if nxt == len(data) or (nxt + 4 <= len(data) and 0 < struct.unpack_from('I', data, nxt)[0] < 1000):
                    size = guess; break
            print('sample size', size)
        body = data[pos + 4: pos + 4 + count * size]
        if len(body) < count * size:
            break
        for i in range(count):
            samples.append(body[i * size:(i + 1) * size])
        pos += 4 + count * size
        batches += 1
    print(f'{len(data)} bytes, {batches} batches, {len(samples)} samples in {seconds} s')
    for s in samples[:3] + samples[-2:]:
        ts = struct.unpack_from('Q', s, 0)[0]
        f = struct.unpack_from('fff', s, 8)
        n = struct.unpack_from('iii', s, 8)
        print(f'  t {ts}  float {tuple(round(v, 2) for v in f)}  int {n}')
