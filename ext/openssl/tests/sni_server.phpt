--TEST--
sni_server
--EXTENSIONS--
openssl
--SKIPIF--
<?php
if (!function_exists("proc_open")) die("skip no proc_open");
?>
--FILE--
<?php
$serverCode = <<<'CODE'
    $flags = STREAM_SERVER_BIND|STREAM_SERVER_LISTEN;
    $ctx = stream_context_create(['ssl' => [
        'SNI_server_certs' => [
            "cs.php.net" => __DIR__ . "/sni_server_cs.pem",
            "uk.php.net" => __DIR__ . "/sni_server_uk.pem",
            "us.php.net" => __DIR__ . "/sni_server_us.pem"
        ]
    ]]);

    $server = stream_socket_server('tls://127.0.0.1:0', $errno, $errstr, $flags, $ctx);
    phpt_notify_server_start($server);

    for ($i=0; $i < 3; $i++) {
        @stream_socket_accept($server, 3);
    }
CODE;

$clientCode = <<<'CODE'
    $flags = STREAM_CLIENT_CONNECT;
    $ctxArr = [
        'cafile' => __DIR__ . '/sni_server_ca.pem',
        'capture_peer_cert' => true
    ];

    $ctxArr['peer_name'] = 'cs.php.net';
    $ctx = stream_context_create(['ssl' => $ctxArr]);
    $client = stream_socket_client("tls://{{ ADDR }}", $errno, $errstr, 1, $flags, $ctx);
    $cert = stream_context_get_options($ctx)['ssl']['peer_certificate'];
    var_dump(openssl_x509_parse($cert)['subject']['CN']);

    $ctxArr['peer_name'] = 'uk.php.net';
    $ctx = stream_context_create(['ssl' => $ctxArr]);
    $client = @stream_socket_client("tls://{{ ADDR }}", $errno, $errstr, 1, $flags, $ctx);
    $cert = stream_context_get_options($ctx)['ssl']['peer_certificate'];
    var_dump(openssl_x509_parse($cert)['subject']['CN']);

    $ctxArr['peer_name'] = 'us.php.net';
    $ctx = stream_context_create(['ssl' => $ctxArr]);
    $client = @stream_socket_client("tls://{{ ADDR }}", $errno, $errstr, 1, $flags, $ctx);
    $cert = stream_context_get_options($ctx)['ssl']['peer_certificate'];
    var_dump(openssl_x509_parse($cert)['subject']['CN']);
CODE;

include 'ServerClientTestCase.inc';
ServerClientTestCase::getInstance()->run($clientCode, $serverCode);
?>
--EXPECTF--
string(%d) "cs.php.net"
string(%d) "uk.php.net"
string(%d) "us.php.net"
