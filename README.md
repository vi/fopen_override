fopen_override
==============

LD_PRELOAD-based fopen/open filename overrider

Example:

    LD_PRELOAD=libfopen_override.so FOPEN_OVERRIDE=/etc/ppp/chap-secrets=/root/encfs/chap-secrets pppd ...

Non-working example:

    LD_PRELOAD=libfopen_override.so FOPEN_OVERRIDE=/etc/resolv.conf=/tmp/tmpresolv.conf ping google.com

Tricky example:

    LD_PRELOAD=libfopen_override.so FOPEN_OVERRIDE='debug,noabs,qqq1=www1,../filename\,with\,comma=../filename\=with\=eqsign' program [args...]
