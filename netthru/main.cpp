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

#include <stdio.h>
#include <string>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

using std::string;

struct Settings {
    enum enum_mode {unknown, server, client} mode = unknown;
    string  remoteip;
    int     secs = 10;
    int     bytes_per_buf = 12288;
    int     port = 54811;
};

int doServer(Settings settings)
{
    int retval = 0;
    int socket_listen, socket_to_client, addr_len;
    struct sockaddr_in server_addr, client_addr;
    
    //Create socket
    int protocol = 0;  // protocol is IP.
    socket_listen = socket(AF_INET , SOCK_STREAM , protocol);
    if (socket_listen == -1)
    {
        printf("Could not create socket");
    }
    puts("Socket created");
    
    //Prepare the sockaddr_in structure
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(settings.port);
    
    //Bind
    if( bind(socket_listen,(struct sockaddr *)&server_addr , sizeof(server_addr)) < 0)
    {
        //print the error message
        perror("bind failed. Error");
        //return 1;
    }
    puts("bind done");
    
    // Listen on the socket. Do not allow a backlog, because since the purpose
    // of this program is to measure total throughput, we do not want simultaneous
    // connections.
    int backlog = 0;
    listen(socket_listen , backlog);
    
    //Accept and incoming connection
    puts("Waiting for incoming connections...");
    addr_len = sizeof(struct sockaddr_in);
    
    //accept connection from an incoming client
    socket_to_client = accept(socket_listen, (struct sockaddr *)&client_addr, (socklen_t*)&addr_len);
    if (socket_to_client < 0) {
        
    }
    
    return retval;
}

int doClient(Settings settings)
{
    int retval = 0;
    
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
    return bOK;
}

void usage()
{
    const char *msg[] = {
        "netthru: program to measure network throughput via TCP.",
        "Run two copies of this program, one in server mode and one in client mode.",
        "",
        "Usage for server mode:",
        "  netthru -mode:server",
        "(Server mode is simple, because the server takes its directions from the client.)",
        "",
        "Usage for client mode:",
        "  netthru -mode:client -remoteip:remoteip -secs:secs -nbytes:nbytes",
        "where remoteip is the IPv4 address of the server",
        "      secs     is the number of seconds to send",
        "      nbytes   is the number of bytes the server should send at once",
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
    for(int j=0; j<argc; j++) {
        string name, val;
        const char *parg;
        parg = argv[j];
        if(parseArg(parg, name, val)) {
            if("mode"==name) {
                if("server"==val) {
                    settings.mode = Settings::server;
                } else if("client"==val) {
                    settings.mode = Settings::client;
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
        if(settings.mode == Settings::server) {
            retval = doServer(settings);
        } else {
            retval = doClient(settings);
        }
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
