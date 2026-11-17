#!/usr/bin/env python3
# Stage 6.6 probe: the accelerometer and gyroscope at once (5 ms asked), each one's rate.
import dbus, os, select, socket, struct, time
bus = dbus.SystemBus()
S = 'com.nokia.SensorService'
mgr = dbus.Interface(bus.get_object(S, '/SensorManager'), 'local.SensorManager')
open_ = []
for name, iface in (('accelerometersensor', 'local.AccelerometerSensor'), ('gyroscopesensor', 'local.GyroscopeSensor')):
    mgr.loadPlugin(name)
    sid = int(mgr.requestSensor(name, dbus.Int64(os.getpid())))
    sock = socket.socket(socket.AF_UNIX); sock.connect('/run/sensord.sock'); sock.sendall(struct.pack('i', sid)); sock.recv(1)
    sensor = dbus.Interface(bus.get_object(S, '/SensorManager/' + name), iface)
    sensor.setStandbyOverride(sid, True); sensor.setInterval(sid, 5); sensor.start(sid)
    open_.append([name, sid, sock, sensor, b''])
end = time.time() + 3
while time.time() < end:
    r = select.select([o[2] for o in open_], [], [], 0.2)[0]
    for o in open_:
        if o[2] in r:
            o[4] += o[2].recv(65536)
for name, sid, sock, sensor, data in open_:
    sensor.stop(sid); mgr.releaseSensor(name, sid, dbus.Int64(os.getpid())); sock.close()
    n, pos = 0, 0
    while pos + 4 <= len(data):
        c = struct.unpack_from('I', data, pos)[0]; n += c; pos += 4 + 24 * c
    print(f'{name}: {n / 3:.1f} Hz')
