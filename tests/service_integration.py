"""Isolated Broker TCP + synthetic HQMarket integration. No production database access.
Usage: python3 service_integration.py --binary /tmp/build/Broker --socket TEST_SOCKET --mysql-port 13307 [--xengine-probe /tmp/xengine/broker_client_probe]
"""
import argparse, contextlib, datetime, json, os, pathlib, socket, struct, subprocess, tempfile, threading, time

def varint(n):
    result = bytearray()
    while n > 127:
        result.append((n & 127) | 128); n >>= 7
    result.append(n); return bytes(result)

def field(n, value):
    if isinstance(value, str): value = value.encode()
    return varint(n << 3) + varint(value) if isinstance(value, int) else varint((n << 3) | 2) + varint(len(value)) + value

def parse(data):
    def integer(pos):
        value = shift = 0
        while True:
            b = data[pos]; pos += 1; value |= (b & 127) << shift
            if b < 128: return value, pos
            shift += 7
    result = {}; pos = 0
    while pos < len(data):
        tag, pos = integer(pos)
        if (tag & 7) == 0: value, pos = integer(pos)
        elif (tag & 7) == 2:
            size, pos = integer(pos); value = data[pos:pos+size]; pos += size
        else: raise AssertionError('unexpected wire type')
        result.setdefault(tag >> 3, []).append(value)
    return result

def one(message, key, default=0): return message.get(key, [default])[0]
def packet(kind, request_id=0, extra=None, payload=b''):
    data = field(1, kind) + field(5, request_id) + payload
    for key, value in (extra or {}).items(): data += field(3, field(1, key) + field(2, str(value)))
    return struct.pack('!I', len(data)) + data

def recv(sock):
    def exact(size):
        data = b''
        while len(data) < size:
            part = sock.recv(size-len(data))
            if not part: raise EOFError()
            data += part
        return data
    return parse(exact(struct.unpack('!I', exact(4))[0]))

class Client:
    def __init__(self, port):
        self.socket = socket.create_connection(('127.0.0.1', port), timeout=5); self.next_id = 100; self.events = []
    def call(self, kind, **extra):
        self.next_id += 1; self.socket.sendall(packet(kind, self.next_id, extra))
        while True:
            response = recv(self.socket)
            if one(response, 5) == self.next_id: return response
            self.events.append(response)
    def ok(self, kind, **extra):
        result = self.call(kind, **extra)
        assert one(result, 6) == 0, (kind, result)
        return result

def free_port():
    with socket.socket() as sock: sock.bind(('127.0.0.1', 0)); return sock.getsockname()[1]

class Market:
    def __init__(self):
        self.listener = socket.socket(); self.listener.bind(('127.0.0.1', 0)); self.port = self.listener.getsockname()[1]; self.listener.listen(); self.listener.settimeout(.2)
        self.stop = threading.Event(); self.subscribed = threading.Event(); self.sequence = 0
        self.thread = threading.Thread(target=self.run, daemon=True); self.thread.start()
    def run(self):
        while not self.stop.is_set():
            try: client, _ = self.listener.accept()
            except socket.timeout: continue
            threading.Thread(target=self.handle, args=(client,), daemon=True).start()
    def handle(self, client):
        client.settimeout(.05); subscribed = False
        try:
            while not self.stop.is_set():
                try:
                    request = recv(client); kind = one(request, 1)
                    if kind in (1, 6): client.sendall(packet(kind, one(request, 5), {'token':'hq-test'}))
                    if kind == 40: subscribed = True; self.subscribed.set()
                except socket.timeout: pass
                if subscribed:
                    self.sequence += 1; now = int(time.time()*1000)
                    level = field(1, 1000) + field(2, 10000) + field(3, 2)
                    depth = field(1, field(1, '600000') + field(2, 1)) + field(2, now) + field(3, now) + field(4, level) + field(5, level)
                    client.sendall(packet(48, extra={'sequence':self.sequence}, payload=field(15, depth)))
        except (OSError, EOFError): pass
        finally: client.close()
    def close(self): self.stop.set(); self.thread.join(2); self.listener.close()

def fault_probe(binary, env):
    listener = socket.socket(); listener.bind(('127.0.0.1', 0)); listener.listen(); listener.settimeout(15)
    count = []
    def peer():
        client, _ = listener.accept()
        try:
            while True:
                request = recv(client); kind = one(request,1); request_id = one(request,5)
                extra = {}
                for entry in request.get(3,[]):
                    row = parse(entry); extra[one(row,1,b'').decode()] = one(row,2,b'').decode()
                if kind in (1,6): client.sendall(packet(kind,request_id,{'token':'broker-test'}))
                elif kind == 9: client.sendall(packet(9,request_id,payload=field(31,field(1,field(1,7)+field(2,'fault-account')))))
                elif kind == 59:
                    now = int(time.time()*1000)
                    funds = field(1,7)+field(2,10000000)+field(3,10000000)+field(5,1)
                    sync = field(2,1)+field(3,now)+field(4,funds)
                    client.sendall(packet(59,request_id,payload=field(39,sync)))
                elif kind == 49: count.append(1); break
        finally: client.close()
    thread = threading.Thread(target=peer,daemon=True); thread.start()
    try:
        subprocess.run([binary,str(listener.getsockname()[1]),'broker-test','7','fault'],env=env,check=True,timeout=20)
        thread.join(2); assert len(count) == 1, 'interrupted order was retried'
    finally: listener.close()

def main():
    parser = argparse.ArgumentParser(); parser.add_argument('--binary', required=True); parser.add_argument('--socket', required=True); parser.add_argument('--mysql-port', type=int, required=True); parser.add_argument('--xengine-probe'); parser.add_argument('--xengine-binary'); args = parser.parse_args()
    database = 'xtrade_broker_test_' + str(os.getpid())
    def sql(query): subprocess.run(['mysql','--socket='+args.socket,'-uroot','-e',query], check=True, stdout=subprocess.DEVNULL)
    def wait_lease():
        deadline = time.monotonic() + 45
        while time.monotonic() < deadline:
            active = subprocess.check_output(['mysql','--socket='+args.socket,'-uroot','-N','-B','-e','SELECT IF(expires_at>CURRENT_TIMESTAMP(3),1,0) FROM '+database+'.sim_counter_lease WHERE id=1'],text=True).strip()
            if active == '0': return
            time.sleep(.2)
        raise AssertionError('terminated writer lease did not expire')
    sql('CREATE DATABASE '+database)
    market = Market(); process = None; client = None; xengine_process = None; xengine_database = None; xengine_user = None
    try:
        with tempfile.TemporaryDirectory(prefix='broker-integration-') as directory:
            runtime = pathlib.Path(directory); (runtime/'ini').mkdir(); (runtime/'sql').mkdir()
            source = pathlib.Path(__file__).resolve().parents[1]/'Broker'
            (runtime/'sql/table_create.sql').write_text((source/'exec/debug/sql/table_create.sql').read_text())
            rules = json.loads((source/'counter/rules.example.json').read_text()); now = int(time.time()*1000); day = int(datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%d'))
            rules['calendar'] = [dict(day=day, open_ms=now-3600000, break_ms=now+3600000, resume_ms=now+7200000, close_ms=now+10800000)]
            rules['rules'][0]['day'] = day; (runtime/'rules.json').write_text(json.dumps(rules))
            port = free_port()
            (runtime/'ini/system.ini').write_text(f'[Broker]\nport={port}\ntoken=broker-test\n[MySQL]\nhost=127.0.0.1\nport={args.mysql_port}\naccount=root\npassword=\ndatabase={database}\npool_size=4\n[HQMarket]\nhost=127.0.0.1\nport={market.port}\ntoken=hq-test\n[Trade]\nrules_path={runtime}/rules.json\n')
            env = dict(os.environ, LD_LIBRARY_PATH=str(source/'libs/debug') + ':' + os.environ.get('LD_LIBRARY_PATH',''))
            log = open(runtime/'broker.log','w')
            def start():
                process = subprocess.Popen([str(pathlib.Path(args.binary).resolve())], cwd=runtime, env=env, stdout=log, stderr=log)
                for _ in range(100):
                    if process.poll() is not None: raise AssertionError('Broker exited: '+(runtime/'broker.log').read_text())
                    try: return process, Client(port)
                    except OSError: time.sleep(.1)
                raise AssertionError('Broker startup timed out')
            process, client = start()
            assert one(client.call(9),6) != 0, 'unauthenticated request accepted'
            assert one(client.call(1,token='wrong'),6) != 0, 'bad token accepted'
            client.ok(1,token='broker-test')
            for unsupported in (0,54,55,56,57,58):
                assert one(client.call(unsupported),6) == 3, 'push or unknown type accepted as a request'
            account = parse(one(client.ok(10,name='integration',initial_balance=10000000),30))
            account_id = one(account,1); assert account_id >= 1000000000000
            assert one(client.call(11,account_id=account_id,name=''),6) == 1202
            client.ok(11,account_id=account_id,name='integration-renamed')
            catalogue = parse(one(client.ok(9),31))
            assert any(one(parse(row),2,b'') == b'integration-renamed' for row in catalogue.get(1,[]))
            client.ok(15,account_id=account_id)
            client.ok(12,account_id=account_id)
            funds = parse(one(client.ok(14,account_ids=json.dumps([account_id])),34))
            assert one(parse(one(funds,1)),5) == 1, 'funds handler did not return synchronized account'
            client.ok(60,account_id=account_id)
            client.ok(13,account_id=account_id)
            client.ok(12,account_id=account_id)
            assert market.subscribed.wait(5), 'Broker did not independently subscribe to HQMarket'
            time.sleep(.2)
            order_args = dict(account_id=account_id,client_order_id=201,security='600000.SSE',side='buy',order_type='limit',price=1000,price_scale=2,quantity=100,strategy_id=42,signal_id=43)
            reply = client.ok(49,**order_args)
            order = parse(one(parse(one(reply,36)),1)); assert one(order,13) == 100 and one(order,5) == 42 and one(order,6) == 43, order
            client.ok(49,**order_args)
            trades = parse(one(client.ok(53,account_id=account_id),37)); assert len(trades.get(1,[])) == 1, 'duplicate execution'
            order_args.update(client_order_id=202,price=900,signal_id=44)
            order = parse(one(parse(one(client.ok(49,**order_args),36)),1)); order_id = one(order,3)
            cancelled = parse(one(parse(one(client.ok(50,account_id=account_id,order_id=order_id),36)),1)); assert one(cancelled,9) == 3
            assert any(one(event,1) == 56 for event in client.events), 'no trade push'
            if args.xengine_probe:
                subprocess.run([args.xengine_probe,str(port),'broker-test',str(account_id)], env=env, check=True, timeout=30)
                fault_probe(args.xengine_probe, env)
            if args.xengine_binary:
                xengine_database = database + '_wind'; xengine_user = database + '_user'
                sql('CREATE DATABASE '+xengine_database)
                sql("CREATE USER '"+xengine_user+"'@'127.0.0.1' IDENTIFIED BY 'test-only-password'")
                sql("GRANT ALL ON "+xengine_database+".* TO '"+xengine_user+"'@'127.0.0.1'")
                xengine_runtime = runtime/'xengine'; (xengine_runtime/'ini').mkdir(parents=True); (xengine_runtime/'sql').mkdir()
                xengine_source = source.parents[1]/'XEngine/XEngine'
                (xengine_runtime/'sql/table_create.sql').write_text((xengine_source/'exec/debug/sql/table_create.sql').read_text())
                xengine_port = free_port(); http_port = free_port()
                (xengine_runtime/'ini/system.ini').write_text(f'[System]\n' + f'tcp_port={xengine_port}\nhttp_port={http_port}\n[Server]\nhost0=HQ_127.0.0.1_{market.port}_1\n[HQMarket]\naccount=test\npassword=test-only-password\n[MySQL]\nhost=127.0.0.1\nport={args.mysql_port}\naccount={xengine_user}\npassword=test-only-password\ndatabase={xengine_database}\npool_size=4\n[Broker]\nhost=127.0.0.1\nport={port}\ntoken=broker-test\n')
                xengine_log = open(runtime/'xengine.log','w')
                xengine_process = subprocess.Popen([str(pathlib.Path(args.xengine_binary).resolve())],cwd=xengine_runtime,env=env,stdout=xengine_log,stderr=xengine_log)
                for _ in range(100):
                    if xengine_process.poll() is not None: raise AssertionError('XEngine exited: '+(runtime/'xengine.log').read_text())
                    try: xtrader = Client(xengine_port); break
                    except OSError: time.sleep(.1)
                else: raise AssertionError('XEngine startup timeout')
                xtrader.ok(3,user='test-user',password='test-password')
                xtrader.ok(1,user='test-user',password='test-password')
                record = parse(one(xtrader.ok(10,name='xtrader-account',initial_balance=10000000),30)); xtrader_account = one(record,1)
                xtrader.ok(8,broker='wind_simulated',site='default',account='SIM-'+str(xtrader_account),password='test-password')
                time.sleep(2)
                xtrader_order = dict(order_args,account_id=xtrader_account,client_order_id=301,price=1000,signal_id=301)
                filled = parse(one(parse(one(xtrader.ok(49,**xtrader_order),36)),1)); assert one(filled,13) == 100 and one(filled,4) == xtrader.next_id
                xtrader_order.update(client_order_id=302,price=900,signal_id=302)
                pending = parse(one(parse(one(xtrader.ok(49,**xtrader_order),36)),1))
                xtrader.ok(50,account_id=xtrader_account,order_id=one(pending,3))
                xtrader.ok(51,account_id=xtrader_account); xtrader.ok(52,account_id=xtrader_account); xtrader.ok(53,account_id=xtrader_account)
                assert any(one(event,1) == 56 for event in xtrader.events), 'XEngine did not forward trade push'
                process.terminate(); process.wait(10)
                time.sleep(1)
                assert one(xtrader.call(49,**dict(xtrader_order,client_order_id=303,signal_id=303)),6) != 0, 'XEngine accepted order with Broker down'
                xtrader.socket.close(); xengine_process.terminate(); xengine_process.wait(10); xengine_process = None
                print('XTrader wire client -> XEngine -> Broker account login, submit, fill push, query, cancel and offline rejection passed')
                # Default SIGTERM termination leaves the writer lease until expiry.
                wait_lease()
                process, client = start(); client.ok(1,token='broker-test')
            client.socket.close(); client = None
            process.terminate(); process.wait(10); wait_lease(); process, client = start(); client.ok(1,token='broker-test')
            sync = parse(one(client.ok(59,account_id=account_id),39)); assert len(sync.get(7,[])) >= 1, 'restart lost trades'
            print('Broker TCP auth, account CRUD/connect, HQ depth matching, idempotency, cancellation, push and restart passed')
    finally:
        if client is not None: client.socket.close()
        if process is not None and process.poll() is None: process.terminate(); process.wait(10)
        market.close()
        if xengine_process is not None and xengine_process.poll() is None: xengine_process.terminate(); xengine_process.wait(10)
        if xengine_database is not None: sql('DROP DATABASE '+xengine_database)
        if xengine_user is not None: sql("DROP USER '"+xengine_user+"'@'127.0.0.1'")
        sql('DROP DATABASE '+database)
if __name__ == '__main__': main()
