import socket

def test_echo():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect(("127.0.0.1", 8080))
    s.sendall(b"Hello, world!\n")
    data = s.recv(1024)
    s.close()
    assert data == b"Hello, world!\n"
    print("Echo test passed!")

if __name__ == "__main__":
    test_echo()

