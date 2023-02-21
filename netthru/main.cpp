// netthru is a macos command-line program to measure the network throughput
// between two computers.  One copy of the program is run in client mode,
// and the other is run in server mode.  The server sends data over TCP as
// fast as possible to the receiver, which measures the throughput.
//
//  Created by Mark Riordan on 1/20/23.

// Xcode puts the executable at
// ~/Library/Developer/Xcode/DerivedData/netthru-dctqldmtairduzemiaiismkbpdgz/Build/Products/Debug/netthru
// I can't figure out how to create a non-debug build.  I'm using Xcode's menu option:
// Product | Build For | Running

#include <chrono>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <stdio.h>
#include <stdarg.h>
#include <string>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/select.h>
#include <memory>

using std::string;

// Macros to stringify a macro value outside a macro.
#define makestr(a) #a
#define xstr(a) makestr(a)

#define DEFAULT_SECS 10
#define DEFAULT_BYTES_PER_BUF 12288
#define DEFAULT_PORT 54811

struct Settings {
    enum enum_mode {unknown, server, client} mode = unknown;
    string  remoteip;
    int     secs = DEFAULT_SECS;
    int     bytes_per_buf = DEFAULT_BYTES_PER_BUF;
    int     port = 54811;
    string  msg="";
    string  logfilename;
};

FILE *fileLog=NULL;

// Safe verion of strcpy without having to worry about which C++
// standard is supported by the compiler.
void safe_strcpy(char *dest, size_t destAlloc, const char *source)
{
    if(destAlloc > 0) {
        while(destAlloc-- > 1){
            *(dest++) = *(source++);
        }
        *dest = '\0';
    }
}

// Returns date/time in string format.  Thanks to
// https://stackoverflow.com/questions/34857119/
// but note that I had to substitute system_clock to get it to work on macOS.
using Clock = std::chrono::system_clock;
static std::string timePointToString(const Clock::time_point &tp, const std::string &format, bool withMs = true, bool utc = false)
{
    const Clock::time_point::duration tt = tp.time_since_epoch();
    const time_t durS = std::chrono::duration_cast<std::chrono::seconds>(tt).count();
    std::ostringstream ss;
    if (const std::tm *tm = (utc ? std::gmtime(&durS) : std::localtime(&durS))) {
        ss << std::put_time(tm, format.c_str());
        if (withMs) {
            const long long durMs = std::chrono::duration_cast<std::chrono::milliseconds>(tt).count();
            ss << std::setw(3) << std::setfill('0') << int(durMs - durS * 1000);
        }
    }
    // gmtime/localtime() returned null
    else {
        ss << "<FORMAT ERROR>";
    }
    return ss.str();
}

void logMsg(const char *fmt, ...)
{
    char buf[200];
    
    Clock::time_point now = std::chrono::system_clock::now();
    std::string stamp = timePointToString(now, "%Y-%m-%d %H:%M:%S.");

    safe_strcpy(buf, sizeof(buf), stamp.c_str());
    buf[stamp.length()] = ' ';
    
    va_list args1;
    va_start(args1, fmt);
    vsnprintf(buf+stamp.length()+1, sizeof(buf) - stamp.length()-2, fmt, args1);
    va_end(args1);
    
    fprintf(fileLog, "%s\n", buf);
    printf("%s\n",buf);
}

void openLogFile(string logfilename)
{
    fileLog = fopen(logfilename.c_str(), "a");
}

void flushLogFile()
{
    fflush(fileLog);
}

void closeLogFile()
{
    fclose(fileLog);
    fileLog = NULL;
}

// Return the elapsed wall clock time (from some arbitrary starting point)
// in seconds.
double getCurrentSeconds()
{
    struct timeval tv;
    struct timezone tz;

    gettimeofday(&tv, &tz);
    double secs = ((double)tv.tv_sec) + (0.000001 * tv.tv_usec);
    return secs;
}

// Send a buffer of bytes, making multiple calls to send if necessary
// to send the entire buffer.
bool sendAll(int sock, unsigned char *buf, size_t nbytes)
{
    bool bOK=true;
    ssize_t totSent = 0;
    size_t nBytesToSend = nbytes;
    size_t offset = 0;
    const int flags = 0;
    do {
        ssize_t bytes_sent = send(sock, buf+offset, nBytesToSend, flags);
        if(bytes_sent < 0) {
            perror("Error sending");
            bOK = false;
            break;
        }
        //printf("sendAll sent %ld bytes\n", bytes_sent);
        totSent += bytes_sent;
        offset += bytes_sent;
    } while(totSent < nbytes);
    return bOK;
}

// Read from a TCP socket until the provided buffer is full, or we
// see the connection close (or return an error).
// Entry:   sock    is the socket to read from.
//          nbytes  is the size of the buffer pbuf.
// Exit:    Returns the number of bytes read, or -1 if error.
//          pbuf    contains the bytes that were read.
//          bEOF is true iff the connection closed.
ssize_t recvAll(int sock, unsigned char *pbuf, ssize_t nbytes, bool &bEOF)
{
    fd_set fd_read, fd_write, fd_error;
    bEOF = false;

    // The first argument to select() is weird.
    const int nfds_to_examine = 1 + sock;
    ssize_t bytesReadSoFar = 0;
    int flags = 0;
    do {
        FD_ZERO(&fd_read);
        FD_ZERO(&fd_write);
        FD_ZERO(&fd_error);
        FD_SET(sock, &fd_read);
        FD_SET(sock, &fd_error);
        
        struct timeval timeout;
        timeout.tv_sec = 5;
        timeout.tv_usec = 0;
        int nfds = select(nfds_to_examine, &fd_read, &fd_write, &fd_error, &timeout);
        if(nfds < 0) {
            perror("Error in select");
            break;
        } else if(0==nfds) {
            puts("Timeout in select");
            bytesReadSoFar = -1;
            break;
        } else {
            // recv returns # of bytes returned, else 0 if connection was closed,
            // else -1 if error.
            ssize_t nbytesThisRead = recv(sock, bytesReadSoFar+pbuf,
                                          nbytes-bytesReadSoFar, flags);
            if(nbytesThisRead < 0) {
                perror("reading from socket");
                break;
            } else if(0==nbytesThisRead) {
                bEOF = true;
                break;
            } else {
                bytesReadSoFar += nbytesThisRead;
            }
        }
    } while(bytesReadSoFar < nbytes);
    //printf("recvAll returning %ld\n", bytesReadSoFar);
    return bytesReadSoFar;
}

int handleServerConnection(int socket_to_client)
{
    int retval = 0;
    char bufFromClient[256];
    ssize_t nbytes;
    
    ssize_t nBytesSoFar = 0;
    ssize_t freeBytes = sizeof(bufFromClient)-1;
    string msgFromClient;
    int secsToSend=0;
    int bytesPerBuf=0;
    
    // Read the message from the client, which tells us what to do
    // and what the parameters are.
    do {
        nbytes = recv(socket_to_client, nBytesSoFar+bufFromClient, freeBytes, 0);
        if(nbytes > 0) {
            nBytesSoFar += nbytes;
            freeBytes -= nbytes;
            bufFromClient[nBytesSoFar] = '\0';
            if(NULL != strchr(bufFromClient, '\n')) break;
        } else if(0==nbytes) {
            puts("Error: unexpected early end of stream");
            break;
        } else {
            perror("Error during initial msg from client");
            break;
        }
    } while(nBytesSoFar < sizeof(bufFromClient)-1);
    
    char *ptok = strtok(bufFromClient, "|");
    // Ignore the first token, which should always be "send".
    if(ptok) {
        // Parse the # of seconds to send.
        ptok = strtok(NULL, "|");
    }
    if(ptok) {
        secsToSend = atoi(ptok);
        // Parse the # of bytes to send at once.
        ptok = strtok(NULL, "|");
    }
    if(ptok) {
        bytesPerBuf = atoi(ptok);
        // Parse the message the client wants us to log.
        ptok = strtok(NULL, "|");
        if(ptok) {
            msgFromClient = ptok;
        }
    }
    
    logMsg("Client says send for %d secs; %d bytes per send; msg: %s",
           secsToSend, bytesPerBuf, msgFromClient.c_str());

    // This will auto-delete the array when it goes out of scope.
    std::unique_ptr<unsigned char> pbuf(new unsigned char[bytesPerBuf]);
    
    // Fill the buffer with data.
    unsigned char mybyte = (unsigned char) 'A';
    for(int j=0; j<bytesPerBuf; j++) {
        pbuf.get()[j] = mybyte;
        mybyte++;
        if(!isprint(mybyte)) mybyte = 'A';
    }
    
    double timeStart = getCurrentSeconds();
    double timeLastUIUpdate = timeStart;
    double secsSinceStart;
    size_t totBytesSent = 0;
    size_t nSends = 0;
    do {
        bool bOK = sendAll(socket_to_client, pbuf.get(), bytesPerBuf);
        if(!bOK) {
            perror("Error sending buffer");
            break;
        }
        totBytesSent += bytesPerBuf;
        nSends++;
        double timeNow = getCurrentSeconds();
        double secsSinceLastUIUpdate = timeNow - timeLastUIUpdate;
        if(secsSinceLastUIUpdate >= 1.0) {
            timeLastUIUpdate = timeNow;
        }
        secsSinceStart = timeNow - timeStart;
        //printf("handleServerConnection: secsSinceStart=%7.2f secsToSend=%d\n",secsSinceStart, secsToSend);
    } while(secsSinceStart < secsToSend);
    
    close(socket_to_client);
    
    double timeEnd = getCurrentSeconds();
    double secs = timeEnd - timeStart;
    double mbPerSec = totBytesSent / secs / (1024.0*1024.0);
    logMsg("Sent %ld bytes in %.3f secs for %.3f MB/sec (%.3f Mb/sec)", totBytesSent, secs, mbPerSec, 8*mbPerSec);
    
    return retval;
}

int doServer(Settings settings)
{
    int retval = 0;
    int socket_listen, socket_to_client, addr_len;
    struct sockaddr_in server_addr, client_addr;
    
    //Create socket
    int protocol = 0;  // protocol is IP.
    socket_listen = socket(AF_INET , SOCK_STREAM , protocol);
    if (socket_listen == -1) {
        printf("Could not create socket");
    }
    
    //Prepare the sockaddr_in structure
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(settings.port);
    
    // Call bind to prepare to start listening.
    if( bind(socket_listen,(struct sockaddr *)&server_addr , sizeof(server_addr)) < 0)
    {
        perror("bind failed.");
        //return 1;
    }
    
    // Listen on the socket. Do not allow a backlog, because since the purpose
    // of this program is to measure total throughput, we do not want simultaneous
    // connections.
    int backlog = 0;
    if(-1 == listen(socket_listen, backlog)) {
        perror("Error listening");
    }
    
    addr_len = sizeof(struct sockaddr_in);
    
    do {
        // Accept connection from an incoming client.
        logMsg("Waiting to accept a connection on port %d", settings.port);
        socket_to_client = accept(socket_listen, (struct sockaddr *)&client_addr, (socklen_t*)&addr_len);
        if (socket_to_client < 0) {
            perror("accept failed");
        } else {
            logMsg("Accepted connection");
            
            // Weirdly, macos seems to kill the app with SIGPIPE when a connection
            // closes.  Prevent that from happening.
            int option_value = 1; /* Set NOSIGPIPE to ON */
            if (setsockopt (socket_to_client, SOL_SOCKET, SO_NOSIGPIPE, &option_value, sizeof (option_value)) < 0) {
                perror ("setsockopt(,,SO_NOSIGPIPE)");
            }
            
            retval = handleServerConnection(socket_to_client);
            logMsg("Client connection closed.");
            flushLogFile();
        }
    } while (true);
    
    return retval;
}

int handleClientConnection(int sock, Settings settings)
{
    int retval = 0;
    char buf[256];
    int nbytes = snprintf(buf, sizeof(buf), "send|%d|%d|%s|\n",
        settings.secs, settings.bytes_per_buf, settings.msg.c_str());
    if(!sendAll(sock, (unsigned char *)buf, nbytes)) {
        retval = 3;
    } else {
        // Command sent to server OK.
        size_t nBytesRec = 0;
        std::unique_ptr<unsigned char> pbuf(new unsigned char[settings.bytes_per_buf]);

        ssize_t totBytesRec = 0;
        ssize_t bytesRecSinceLastUIUpdate = 0;
        double timeStart = getCurrentSeconds();
        double timeLastUIUpdate = timeStart;
        ssize_t nCallsToTimer = 0;
        bool bEOF;
        do {
            nBytesRec = recvAll(sock, pbuf.get(), settings.bytes_per_buf, bEOF);
            double timeNow = getCurrentSeconds();
            nCallsToTimer++;
            if(nBytesRec >= 0 || bEOF) {
                totBytesRec += nBytesRec;
                bytesRecSinceLastUIUpdate += nBytesRec;
                double secsSinceLastUIUpdate = timeNow - timeLastUIUpdate;
                if(nBytesRec > 0) {
                    if(secsSinceLastUIUpdate >= 1.0) {
                        timeLastUIUpdate = timeNow;
                        double mbPerSec = (((double) bytesRecSinceLastUIUpdate) / ((double) secsSinceLastUIUpdate)) / (1024*1024);
                        // Weirdly, nothing prints on macos if I use "\r".
                        printf("%9.3f MB/sec (%.3f Mb/sec)\n", mbPerSec, 8*mbPerSec);
                        bytesRecSinceLastUIUpdate = 0;
                    }
                }
                if(bEOF) {
                    // Clean disconnect received; end of data.
                    double secsTot = timeNow - timeStart;
                    double mBytesPerSec = (((double) totBytesRec) / ((double) secsTot)) / (1024*1024);
                    double mBitsPerSec = 8*mBytesPerSec;
                    logMsg("%8.3f MB/sec (%.3f Mb/sec) final average; %ld timer calls", mBytesPerSec, mBitsPerSec, nCallsToTimer);
                    break;
                }
            } else {
                perror("Unexpected error in connection to server");
                break;
            }
        } while(true);
    }
    
    return retval;
}

int doClient(Settings settings)
{
    int retval = 0;
    
    logMsg("Client parameters: remoteip=%s secs=%d bytePerBuf=%d msg=%s",
           settings.remoteip.c_str(), settings.secs, settings.bytes_per_buf,
           settings.msg.c_str());
    int sock;
    struct sockaddr_in server_addr;
    
    //Create socket
    int protocol = 0;  // protocol is IP.
    sock = socket(AF_INET, SOCK_STREAM, protocol);
    if (sock == -1)
    {
        printf("Could not create socket");
    }
    
    server_addr.sin_addr.s_addr = inet_addr(settings.remoteip.c_str());
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(settings.port);

    // Connect to remote server
    logMsg("Connecting to %s port %d", settings.remoteip.c_str(), settings.port);
    if (connect(sock , (struct sockaddr *)&server_addr , sizeof(server_addr)) < 0)
    {
        perror("connect failed. Error");
        return errno;
    }
    logMsg("Connected to  %s port %d", settings.remoteip.c_str(), settings.port);
    
    retval = handleClientConnection(sock, settings);

    return retval;
}

bool parseArg(const char *arg, string &name, string &val)
{
    bool bOK=true;
    name = "";
    val = "";
    const char *pch = arg;
    bool bHasName = false;
    if('-' == *pch) {
        pch++;
        if('\0' == *pch) {
            bOK = false;
        } else {
            bHasName = true;
            const char *pcolon = strchr(pch, ':');
            if(NULL != pcolon) {
                name = string(pch, pcolon-pch);
                val = string(1+pcolon);
            } else {
                name = string(pch);
            }
        }
    } else {
        val = string(pch);
    }
    //printf("parseArg: name=%s val=%s returning %s\n", name.c_str(), val.c_str(), bOK?"true":"false");
    return bOK;
}

void usage()
{
    const char *msg[] = {
        "netthru: Program to measure network throughput via TCP.",
        "Run two copies of this program, one in server mode and one in client mode.",
        "",
        "Usage for server mode:",
        "  netthru -mode:server [-port:port]",
        "where port     is the TCP port. Defaults to " xstr(DEFAULT_PORT) ".",
        "(Server mode is simple, because the server takes its directions from ",
        "the client.)",
        "",
        "Usage for client mode:",
        "  netthru -mode:client -remoteip:remoteip [-port:port] [-secs:secs] ",
        "    [-nbytes:nbytes] [-msg:msg]",
        "where remoteip is the IPv4 address of the server.",
        "      port     is the TCP port. Defaults to " xstr(DEFAULT_PORT) ".",
        "      secs     is the number of seconds for which the server should send.",
        "               Defaults to " xstr(DEFAULT_SECS) ".",
        "      nbytes   is the number of bytes the server should send at once.",
        "               Defaults to " xstr(DEFAULT_BYTES_PER_BUF) ".",
        "      msg      is an arbitrary message for the server to log.",
        "",
        "MRR  2023-01-20",
        NULL
    };
    
    for(int j=0; NULL != msg[j]; j++) {
        puts(msg[j]);
    }
}

bool parseCmdLine(int argc, const char * argv[], Settings &settings)
{
    bool bOK=true;
    for(int j=1; j<argc; j++) {
        string name, val;
        const char *parg;
        parg = argv[j];
        if(parseArg(parg, name, val)) {
            if("mode"==name) {
                if("server"==val) {
                    settings.mode = Settings::server;
                    settings.logfilename = "netthruserver.log";
                } else if("client"==val) {
                    settings.mode = Settings::client;
                    settings.logfilename = "netthruclient.log";
                } else {
                    printf("Invalid mode: %s\n", val.c_str());
                    bOK = false;
                }
            } else if("remoteip"==name) {
                settings.remoteip = val;
            } else if("secs"==name) {
                settings.secs = atoi(val.c_str());
            } else if("nbytes"==name) {
                settings.bytes_per_buf = atoi((val.c_str()));
            } else if("port"==name) {
                settings.port = atoi(val.c_str());
            } else if("msg"==name) {
                settings.msg = val;
            } else {
                printf("Unrecognized argument: %s\n", name.c_str());
                bOK = false;
            }
        } else {
            printf("Invalid argument: %s\n", parg);
            bOK = false;
        }
    }
    if(settings.mode == Settings::unknown) {
        bOK = false;
        printf("Mode must be server or client\n");
    }
    return bOK;
}

int doMain(int argc, const char * argv[])
{
    int retval = 0;
    Settings settings;
    if(parseCmdLine(argc, argv, settings)) {
        openLogFile(settings.logfilename);
        if(settings.mode == Settings::server) {
            retval = doServer(settings);
        } else {
            retval = doClient(settings);
        }
        closeLogFile();
    } else {
        usage();
    }
    return retval;
}

int test(int argc, const char * argv[])
{
    int retval = 0;
    
    printf("%s called with ", argv[0]);
    for(int j=1; j<argc; j++) {
        printf("%s ", argv[j]);
    }
    printf("\n");
    
    //  Test parseArg.
    string name, val;
    bool bOK;
    const char *myarg = "myhost";
    bOK = parseArg(myarg, name, val);
    if(bOK && name == "" && val == "myhost") {
        printf("parseArg \"%s\" passed\n", myarg);
    } else {
        printf("** %s failed: name=%s val=%s\n", myarg, name.c_str(), val.c_str());
        retval = 1;
    }
    
    myarg = "-slow";
    bOK = parseArg(myarg, name, val);
    if(bOK && name == "slow" && val == "") {
        printf("parseArg \"%s\" passed\n", myarg);
    } else {
        printf("** %s failed: name=%s val=%s\n", myarg, name.c_str(), val.c_str());
        retval = 1;
    }
    
    myarg = "-mode:server";
    bOK = parseArg(myarg, name, val);
    if(bOK && name == "mode" && val == "server") {
        printf("parseArg \"%s\" passed\n", myarg);
    } else {
        printf("** %s failed: name=%s val=%s\n", myarg, name.c_str(), val.c_str());
        retval = 1;
    }
    
    // Test returning time to milliseconds.
    const auto tp = Clock::now();
    std::cout << timePointToString(tp, "%Z %Y-%m-%d %H:%M:%S.") << std::endl;

    //printf("timePointToString returned %s\n", stamp.c_str());
    
    return 0;
}

int main(int argc, const char * argv[])
{
    int retval = 0;
    if(argc > 1 && 0==strcmp(argv[1],"-test")) {
        retval = test(argc, argv);
    } else {
        retval = doMain(argc, argv);
    }
    return retval;
}
