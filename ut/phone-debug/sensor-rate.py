#!/usr/bin/env python3
# Stage 6.6 probe: the gyroscope's possible rates (getAvailableIntervals) and the rate reached with
# setInterval or setDataRate.
import dbus, os, select, socket, struct, sys, time
bus = dbus.SystemBus()
S = 'com.nokia.SensorService'
mgr = dbus.Interface(bus.get_object(S, '/SensorManager'), 'local.SensorManager')
name, iface = sys.argv[1], sys.argv[2]
mgr.loadPlugin(name)
for how, value in (('setInterval', 5), ('setDataRate', 100.0), ('setDataRate', 200.0)):
    sid = int(mgr.requestSensor(name, dbus.Int64(os.getpid())))
    sock = socket.socket(socket.AF_UNIX); sock.connect('/run/sensord.sock'); sock.sendall(struct.pack('i', sid)); sock.recv(1)
    sensor = dbus.Interface(bus.get_object(S, '/SensorManager/' + name), iface)
    if how == 'setInterval' and value == 5:
        print('available intervals', [tuple(float(x) for x in r) for r in sensor.getAvailableIntervals()])
    sensor.setStandbyOverride(sid, True)
    getattr(sensor, how)(sid, value)
    sensor.start(sid)
    data = b''; end = time.time() + 2
    while time.time() < end:
        if select.select([sock], [], [], 0.2)[0]:
            data += sock.recv(65536)
    sensor.stop(sid); mgr.releaseSensor(name, sid, dbus.Int64(os.getpid())); sock.close()
    n, pos = 0, 0
    while pos + 4 <= len(data):
        c = struct.unpack_from('I', data, pos)[0]; n += c; pos += 4 + 24 * c
    print(f'{how}({value}): {n / 2:.1f} Hz, interval now {int(sensor.interval()) if False else "-"}')
