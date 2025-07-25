

import socket
import subprocess
import time
import unittest
import os

class TestIRCServer(unittest.TestCase):
    SERVER_HOST = '127.0.0.1'
    SERVER_PORT = 8080
    SERVER_PROCESS = None

    @classmethod
    def setUpClass(cls):
        """サーバープロセスを起動する"""
        print(f"Starting IRC server on {cls.SERVER_HOST}:{cls.SERVER_PORT}...")
        # サーバーをバックグラウンドで起動
        # サーバーの実行ファイル名が 'ircserv' であると仮定
        cls.SERVER_PROCESS = subprocess.Popen(
            ['./ircserv', str(cls.SERVER_PORT), 'password'],
            cwd='.', # プロジェクトのルートディレクトリで実行
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE
        )
        time.sleep(3) # サーバーが起動するのを待つ

        # サーバーが実際に起動したか確認
        try:
            with socket.create_connection((cls.SERVER_HOST, cls.SERVER_PORT), timeout=1):
                pass
            print("IRC server started successfully.")
        except (socket.timeout, ConnectionRefusedError):
            print("Failed to connect to IRC server. Checking server output...")
            stdout, stderr = cls.SERVER_PROCESS.communicate(timeout=1)
            print(f"Server stdout: {stdout.decode().strip()}")
            print(f"Server stderr: {stderr.decode().strip()}")
            cls.tearDownClass() # 起動失敗時はクリーンアップ
            raise RuntimeError("IRC server did not start correctly.")

    @classmethod
    def tearDownClass(cls):
        """サーバープロセスを終了する"""
        if cls.SERVER_PROCESS and cls.SERVER_PROCESS.poll() is None:
            print("Terminating IRC server...")
            cls.SERVER_PROCESS.terminate()
            try:
                cls.SERVER_PROCESS.wait(timeout=5)
            except subprocess.TimeoutExpired:
                cls.SERVER_PROCESS.kill()
                cls.SERVER_PROCESS.wait()
            print("IRC server terminated.")
        elif cls.SERVER_PROCESS:
            print("IRC server already stopped.")

    def setUp(self):
        """各テストの前に新しいソケット接続を作成する"""
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.settimeout(5) # タイムアウトを設定
        self.sock.connect((self.SERVER_HOST, self.SERVER_PORT))
        self.recv_buffer = b''

    def tearDown(self):
        """各テストの後にソケット接続を閉じる"""
        self.sock.close()

    def _send_command(self, command):
        """コマンドを送信し、CRLFを追加する"""
        self.sock.sendall(command.encode('utf-8') + b'\r\n')

    def _receive_response(self, timeout=3):
        """サーバーからの応答を受信する"""
        start_time = time.time()
        while True:
            try:
                data = self.sock.recv(4096)
                if data:
                    self.recv_buffer += data
                    # 完全なメッセージを受信したかチェック (CRLFで終わる)
                    if b'\r\n' in self.recv_buffer:
                        messages = self.recv_buffer.split(b'\r\n')
                        self.recv_buffer = messages.pop() # 未完了のメッセージをバッファに残す
                        return [msg.decode('utf-8') for msg in messages if msg]
                elif time.time() - start_time > timeout:
                    break
            except socket.timeout:
                break
            except BlockingIOError:
                # データがない場合は少し待つ
                time.sleep(0.01)
            if time.time() - start_time > timeout:
                break
        return [msg.decode('utf-8') for msg in self.recv_buffer.split(b'\r\n') if msg] # 残りのメッセージも返す

    def test_01_connect_and_quit(self):
        """サーバーに接続し、QUITで切断するテスト"""
        # 接続後の初期メッセージを受信 (例: CAP LS, NOTICE AUTH)
        responses = self._receive_response()
        self.assertGreater(len(responses), 0, "サーバーが接続時に初期メッセージを返していません。")

        self._send_command("QUIT :Leaving")
        responses = self._receive_response()
        # QUIT後の応答は通常ないか、サーバーからの切断通知
        # ソケットが閉じられることを期待
        with self.assertRaises(socket.error):
            self.sock.recv(1) # 接続が閉じられたことを確認

    def test_02_pass_nick_user_registration(self):
        """PASS, NICK, USERコマンドによる登録テスト"""
        self._receive_response() # 初期メッセージをクリア

        self._send_command("PASS password")
        self._send_command("NICK testuser")
        self._send_command("USER testuser 0 * :Test User")

        responses = self._receive_response(timeout=2) # 登録後の応答を待つ
        # 登録成功を示すメッセージ (例: RPL_WELCOME (001)) を確認
        # 応答に "001" が含まれていることを確認
        self.assertTrue(any("001" in r for r in responses), "サーバーが登録後にRPL_WELCOME (001) メッセージを返していません。")

    def test_03_ping_pong(self):
        """PINGコマンドに対するPONG応答テスト"""
        self._receive_response() # 初期メッセージをクリア

        # 登録
        self._send_command("PASS password")
        self._send_command("NICK pinguser")
        self._send_command("USER pinguser 0 * :Ping User")
        self._receive_response(timeout=2) # 登録完了を待つ

        self._send_command("PING :irc.example.com")
        responses = self._receive_response()
        # PONG応答を確認
        self.assertTrue(any("PONG irc.example.com" in r for r in responses), "サーバーがPINGコマンドに対してPONG応答を返していません。")

    def test_04_duplicate_nick(self):
        """重複ニックネームテスト"""
        self._receive_response() # 初期メッセージをクリア

        # 最初のユーザーを登録
        self._send_command("PASS password")
        self._send_command("NICK user1")
        self._send_command("USER user1 0 * :User One")
        self._receive_response(timeout=2)

        self.sock.close() # 最初の接続を閉じる

        # 2番目の接続
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.settimeout(5)
        self.sock.connect((self.SERVER_HOST, self.SERVER_PORT))
        self.recv_buffer = b''
        self._receive_response() # 初期メッセージをクリア

        self._send_command("PASS password")
        self._send_command("NICK user1") # 重複ニックネーム
        self._send_command("USER user2 0 * :User Two")

        responses = self._receive_response(timeout=2)
        # ERR_NICKNAMEINUSE (433) を確認
        self.assertTrue(any("433" in r for r in responses), "サーバーが重複ニックネームに対してERR_NICKNAMEINUSE (433) を返していません。")

    def test_05_unregistered_command(self):
        """未登録状態でのコマンドテスト"""
        self._receive_response() # 初期メッセージをクリア

        self._send_command("JOIN #channel") # 未登録状態でJOIN
        responses = self._receive_response()
        # ERR_NOTREGISTERED (451) を確認
        self.assertTrue(any("451" in r for r in responses), "サーバーが未登録状態でのコマンドに対してERR_NOTREGISTERED (451) を返していません。")

if __name__ == '__main__':
    # unittest.main() を直接呼び出すと、setUpClass/tearDownClass が適切に動作しない場合があるため、
    # TestRunner を使用して明示的に実行する
    suite = unittest.TestSuite()
    suite.addTest(unittest.makeSuite(TestIRCServer))
    runner = unittest.TextTestRunner(verbosity=2)
    runner.run(suite)

