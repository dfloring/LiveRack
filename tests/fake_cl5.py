import socket, time, threading
srv = socket.socket(); srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(('127.0.0.1', 49280)); srv.listen(1); srv.settimeout(20)
c, _ = srv.accept(); c.settimeout(0.2)
start = time.time()
def reader():
    while time.time() - start < 9:
        try:
            d = c.recv(4096)
            if not d: break
        except socket.timeout: pass
threading.Thread(target=reader, daemon=True).start()
# a fader move, CUE on channel 4, CUE off (ignored), CUE on 12, then (follow off) CUE on 4
for at, line in [(1.2, 'NOTIFY set MIXER:Current/InCh/Fader/Level 3 0 -1000 "-10.00"'),
                 (1.5, 'NOTIFY set MIXER:Current/Cue/InCh/On 3 0 1 "ON"'),
                 (2.6, 'NOTIFY set MIXER:Current/Cue/InCh/On 3 0 0 "OFF"'),
                 (2.8, 'NOTIFY set MIXER:Current/Cue/InCh/On 11 0 1 "ON"'),
                 (4.0, 'NOTIFY set MIXER:Current/Cue/InCh/On 3 0 1 "ON"')]:
    time.sleep(max(0, at - (time.time() - start)))
    c.sendall((line + "\n").encode())
time.sleep(4)
