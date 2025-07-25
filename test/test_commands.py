import socket
import subprocess
import time
import os

def find_free_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.bind(('', 0))
    port = s.getsockname()[1]
    s.close()
    return port

def run_test():
    port = find_free_port()
    password = "testpass"
    server_process = None
    try:
        print(f"Starting ircserv on port {port} with password '{password}'...")
        server_process = subprocess.Popen(
            ["./ircserv", str(port), password],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        time.sleep(1) # Give server time to start

        if server_process.poll() is not None:
            print("Server failed to start.")
            print("Stdout:", server_process.stdout.read())
            print("Stderr:", server_process.stderr.read())
            return False

        print("Connecting to server...")
        client_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        client_socket.connect(("127.0.0.1", port))
        client_socket.settimeout(5)

        def send_command(sock, cmd):
            sock.sendall((cmd + "\r\n").encode())
            print(f"Sent: {cmd}")

        def recv_response(sock):
            response = b""
            while True:
                try:
                    chunk = sock.recv(4096)
                    if not chunk:
                        break
                    response += chunk
                    if b"\r\n" in chunk: # Simple way to detect end of message
                        break
                except socket.timeout:
                    break
            decoded_response = response.decode().strip()
            return decoded_response

        # Test PASS command
        send_command(client_socket, f"PASS {password}")
        # No immediate response expected for PASS, it's usually followed by NICK/USER

        # Test NICK command
        send_command(client_socket, "NICK testuser")
        response = recv_response(client_socket)
        # Expect no error for NICK yet, as USER is not sent

        # Test USER command
        send_command(client_socket, "USER testuser 0 * :Test User")
        response = recv_response(client_socket)
        # Expect RPL_WELCOME (001) after successful registration
        if "001 testuser :Welcome to the ft_irc Network, testuser!" not in response:
            print("FAIL: Did not receive RPL_WELCOME (001) after NICK and USER.")
            return False

        print("SUCCESS: PASS, NICK, USER commands tested successfully.")

        # Test PASS command does not send immediate NOTICE on success
        client_socket_notice_check = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        client_socket_notice_check.connect(("127.0.0.1", port))
        client_socket_notice_check.settimeout(1) # Short timeout as no response is expected

        send_command(client_socket_notice_check, f"PASS {password}")
        response_notice_check = recv_response(client_socket_notice_check)

        if response_notice_check and response_notice_check.startswith(":NOTICE"):
            print("FAIL: サーバーが、IRCプロトコルに反して、PASSコマンドの成功時に予期せぬNOTICEメッセージを送信しています。")
            print("PASSコマンドの後にサーバーから即座に応答があるべきではありません。")
            print(f"  現在のサーバー応答: {response_notice_check}")
            return False
        print("SUCCESS: PASS command does not send immediate NOTICE on success.")
        client_socket_notice_check.close()

        # Test PASS command with incorrect password
        client_socket_fail_pass = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        client_socket_fail_pass.connect(("127.0.0.1", port))
        client_socket_fail_pass.settimeout(5)

        send_command(client_socket_fail_pass, "PASS wrongpass")
        send_command(client_socket_fail_pass, "NICK failuser")
        send_command(client_socket_fail_pass, "USER failuser 0 * :Fail User")
        response_fail_pass = recv_response(client_socket_fail_pass)

        if "464 failuser :Password incorrect" not in response_fail_pass:
            print("FAIL: 誤ったパスワードでPASSコマンドを送信した後、サーバーはERR_PASSWDMISMATCH (464) を返すべきです。")
            print("これは、サーバーがパスワードを正しく検証していないか、登録プロセス中に適切なエラーを返していないためかもしれません。")
            print(f"  現在のサーバー応答: {response_fail_pass}")
            return False
        print("SUCCESS: PASS command with incorrect password tested successfully.")
        client_socket_fail_pass.close()

        # Test CAP LS command
        send_command(client_socket, "CAP LS")
        response = recv_response(client_socket)
        if "CAP * LS" not in response or "CAP * END" not in response:
            print("FAIL: Did not receive expected CAP LS response.")
            print(f"Response: {response}")
            return False
        print("SUCCESS: CAP LS command tested successfully.")

        # --- Additional NICK tests ---

        # Test for Changing an Existing Nickname
        send_command(client_socket, "NICK newuser")
        response = recv_response(client_socket)
        # Expect no error for NICK change, and potentially a NICK message from server
        if "newuser" not in response and "432" not in response and "433" not in response:
            print("FAIL: サーバーが既存のクライアントのニックネーム変更を正しく処理していません。")
            print("クライアントが新しい有効なニックネームにNICKコマンドを送信した際、サーバーは変更を認識し、エラーを返すべきではありません。")
            print("これは、認証されたクライアントからのNICKコマンドをサーバーが正しく処理していないか、内部のクライアントリスト/データ構造を更新していないためかもしれません。")
            print(f"  現在のサーバー応答: {response}")
            return False
        print("SUCCESS: Changing an existing nickname tested successfully.")



        # Test NICK with invalid characters (e.g., space)
        send_command(client_socket, "NICK invalid user")
        response = recv_response(client_socket)
        if "432 invalid user :Erroneous nickname" not in response:
            print("FAIL: IRCプロトコルでは、ニックネームにスペースなどの無効な文字を含めることは許可されていません。")
            print("サーバーは、このような無効なニックネームを受け取った場合、ERR_ERRONEUSNICKNAME (432) を返して拒否する必要があります。")
            print("このテストは、サーバーが無効なニックネームの構文を正しく検証し、適切なエラーコードを返すことを確認します。")
            print(f"  現在のサーバー応答: {response}")
            return False
        print("SUCCESS: NICK with invalid characters tested.")

        # Test NICK with no nickname provided
        send_command(client_socket, "NICK")
        response = recv_response(client_socket)
        if "431 :No nickname given" not in response:
            print("FAIL: NICKコマンドには、使用するニックネームが必須です。")
            print("ニックネームが提供されない場合、サーバーは ERR_NONICKNAMEGIVEN (431) を返してコマンドを拒否する必要があります。")
            print("このテストは、NICKコマンドに引数が不足している場合にサーバーが正しくエラーを報告することを確認します。")
            print(f"  現在のサーバー応答: {response}")
            return False
        print("SUCCESS: NICK with no nickname tested.")

        # Test duplicate NICK (after a successful registration)
        # Create a second client to test duplicate NICK
        client_socket2 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        client_socket2.connect(("127.0.0.1", port))
        client_socket2.settimeout(5)

        send_command(client_socket2, f"PASS {password}")
        send_command(client_socket2, "NICK testuser") # Attempt to use existing nickname
        send_command(client_socket2, "USER seconduser 0 * :Second User")
        response2 = recv_response(client_socket2)

        # Expect ERR_NICKNAMEINUSE (433) for the second client
        if "433 testuser :Nickname is already in use" not in response2:
            print("FAIL: 既にネットワーク上で使用されているニックネームを別のクライアントが登録しようとした場合、サーバーはこれを拒否する必要があります。")
            print("この状況では、サーバーは ERR_NICKNAMEINUSE (433) を返すべきです。")
            print("このテストは、サーバーがニックネームの重複を検出し、適切なエラーコードで新しい登録を阻止することを確認します。")
            print(f"  現在のサーバー応答: {response2}")
            return False
        print("SUCCESS: Duplicate NICK tested.")
        client_socket2.close()

        # Test NICK with a nickname longer than 9 characters
        send_command(client_socket, "NICK long_nick") # 10 chars
        response = recv_response(client_socket)
        if "432 long_nick :Erroneous nickname" not in response:
            print("FAIL: IRCプロトコルでは、ニックネームの最大長は9文字です。")
            print("サーバーは、9文字を超えるニックネームを受け取った場合、ERR_ERRONEUSNICKNAME (432) を返して拒否する必要があります。")
            print("このテストは、サーバーが長すぎるニックネームを正しく検証し、適切なエラーコードを返すことを確認します。")
            print(f"  現在のサーバー応答: {response}")
            return False
        print("SUCCESS: NICK with long nickname tested.")

        # Test NICK with a nickname starting with a digit
        send_command(client_socket, "NICK 1test")
        response = recv_response(client_socket)
        if "432 1test :Erroneous nickname" not in response:
            print("FAIL: IRCプロトコルでは、ニックネームは数字で始めることはできません。")
            print("サーバーは、数字で始まるニックネームを受け取った場合、ERR_ERRONEUSNICKNAME (432) を返して拒否する必要があります。")
            print("このテストは、サーバーが数字で始まるニックネームを正しく検証し、適切なエラーコードを返すことを確認します。")
            print(f"  現在のサーバー応答: {response}")
            return False
        print("SUCCESS: NICK with digit-starting nickname tested.")

        # Test NICK with a nickname starting with a hyphen
        send_command(client_socket, "NICK -test")
        response = recv_response(client_socket)
        if "432 -test :Erroneous nickname" not in response:
            print("FAIL: IRCプロトコルでは、ニックネームはハイフンで始めることはできません。")
            print("サーバーは、ハイフンで始まるニックネームを受け取った場合、ERR_ERRONEUSNICKNAME (432) を返して拒否する必要があります。")
            print("このテストは、サーバーがハイフンで始まるニックネームを正しく検証し、適切なエラーコードを返すことを確認します。")
            print(f"  現在のサーバー応答: {response}")
            return False
        print("SUCCESS: NICK with hyphen-starting nickname tested.")

        # Test NICK with only digits
        send_command(client_socket, "NICK 12345")
        response = recv_response(client_socket)
        if "432 12345 :Erroneous nickname" not in response:
            print("FAIL: IRCプロトコルでは、ニックネームは数字のみで構成されることはできません。")
            print("サーバーは、数字のみのニックネームを受け取った場合、ERR_ERRONEUSNICKNAME (432) を返して拒否する必要があります。")
            print("このテストは、サーバーが数字のみのニックネームを正しく検証し、適切なエラーコードを返すことを確認します。")
            print(f"  現在のサーバー応答: {response}")
            return False
        print("SUCCESS: NICK with only digits tested.")

        # Test NICK with only hyphens
        send_command(client_socket, "NICK -----")
        response = recv_response(client_socket)
        if "432 ----- :Erroneous nickname" not in response:
            print("FAIL: IRCプロトコルでは、ニックネームはハイフンのみで構成されることはできません。")
            print("サーバーは、ハイフンのみのニックネームを受け取った場合、ERR_ERRONEUSNICKNAME (432) を返して拒否する必要があります。")
            print("このテストは、サーバーがハイフンのみのニックネームを正しく検証し、適切なエラーコードを返すことを確認します。")
            print(f"  現在のサーバー応答: {response}")
            return False
        print("SUCCESS: NICK with only hyphens tested.")

        return True

    except Exception as e:
        print(f"An error occurred: {e}")
        if server_process:
            print("Server Stdout:", server_process.stdout.read())
            print("Server Stderr:", server_process.stderr.read())
        return False
    finally:
        if server_process:
            print("Terminating ircserv process...")
            server_process.terminate()
            server_process.wait(timeout=5)
            if server_process.poll() is None:
                server_process.kill()
            print("ircserv terminated.")

if __name__ == "__main__":
    if run_test():
        print("All tests passed!")
        exit(0)
    else:
        print("Tests failed!")
        exit(1)
