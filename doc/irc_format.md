# RFC 2812: Internet Relay Chat: Client Protocol

IRCクライアントプロトコルの主要なRFCはRFC 2812です。このRFCは、メッセージフォーマットを定義しており、オプションのプレフィックス、コマンド、および最大15個のパラメータで構成されます。

## メッセージの基本構造

```
[<prefix>] <command> <parameters> [:<trailing>]
```

*   **`<prefix>` (オプション):**
    *   メッセージの送信元を示す。
    *   サーバー名またはクライアントのニックネーム、ユーザー名、ホスト名を含む。
    *   形式: `servername` または `nickname [ '!' user ] [ '@' host ]`
    *   例: `:irc.example.com`, `:nick!user@host`
*   **`<command>`:**
    *   実行されるコマンド名（例: `PASS`, `NICK`, `USER`, `JOIN`, `PRIVMSG`など）。
    *   3文字の数字コード（例: `001` for `RPL_WELCOME`）の場合もある。
*   **`<parameters>`:**
    *   コマンドの引数。
    *   最大15個のパラメータを持つことができる。
    *   各パラメータは単一のASCIIスペース文字で区切られる。
*   **`:<trailing>` (オプション):**
    *   末尾のパラメータ。
    *   コロン（`:`）で始まり、その後に続くすべてのテキストが単一のパラメータとして扱われる。
    *   スペースを含むことができる。
    *   例: `:This is a long message with spaces.`

## メッセージの終端

*   すべてのメッセージはCR-LF（キャリッジリターン - ラインフィード、`\r\n`）で終端されます。

## 例

*   `PASS secretpassword\r\n`
*   `NICK mynickname\r\n`
*   `USER username hostname servername :realname\r\n`
*   `JOIN #channel\r\n`
*   `PRIVMSG #channel :Hello, everyone!\r\n`