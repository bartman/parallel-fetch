### About

This program was written to generate lots of requests against a web
server.  It's capable of running multiple clients (using threads), each
sending multiple concurrent requests (using a select loop).

### Usage

Getting help:

    # pf -h
    pf [-t <threads>] [-a <agents>] [-c <connections>] [-d <what>=<delay>] [-h] <host> <port>

Run 1000 request, in 10 threads, simulating 100 agents per thread.

    # pf -t 10 -a 100 -c 10000 10.10.10.10 80

### License

This software is licensed under GPLv2.
