// ============================================================
//  SERVER MAIN MODULE
//  ------------------------------------------------------------
//  Entry point for the Tic-Tac-Toe server. Handles:
//   - socket initialization
//   - connection accept loop
//   - client thread creation
//   - heartbeat system for disconnection detection
// ============================================================

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <time.h>

#include "utils.h"
#include "room.h"
#include "client.h"
#include "config.h"

#define PING_INTERVAL 5     // Seconds between PINGs
#define MAX_MISSED_PONGS 3   // Disconnect after 3 missed PONGs


// ============================================================
//  heartbeat_thread()
//  ------------------------------------------------------------
//  Periodically sends PING messages to all connected clients.
//  If a client misses 3 consecutive PONG replies, it is
//  considered disconnected and removed from the game.
// ============================================================
void* heartbeat_thread(void* arg) {
    int limit = MAX_CLIENTS;
    if (arg) {
        int v = *(int*)arg;
        if (v > 0 && v < limit) limit = v;
    }
    while (1) {
        pthread_mutex_lock(&g_clients_mtx);

        for (int i = 0; i < limit; i++) {
            struct Client* c = g_clients[i];
            if (!c || !c->connected) continue;

            sendp(c->fd, "PING|");
            c->missed_pongs++;

            if (c->missed_pongs > MAX_MISSED_PONGS) {
                handle_disconnect(c);
            }
        }

        pthread_mutex_unlock(&g_clients_mtx);
        sleep(PING_INTERVAL);
    }
    return NULL;
}


// ============================================================
//  main()
//  ------------------------------------------------------------
//  Initializes server socket, starts heartbeat, and enters
//  accept loop for new client connections.
// ============================================================
int main(int argc, char** argv) {
    ServerConfig cfg;
    config_load("server.config", &cfg);
    int port = cfg.port;
    srand((unsigned)time(NULL));

    // CLI argument overrides config file port
    if (argc >= 2) {
        port = atoi(argv[1]);
        if (port <= 0 || port > 65535) {
            fprintf(stderr, "Invalid port number.\n");
            return 1;
        }
    }

    // --------------------------------------------------------
    //  Setup listening socket
    // --------------------------------------------------------
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    {
        struct in_addr ina;
        if (inet_pton(AF_INET, cfg.bind_address, &ina) == 1) {
            addr.sin_addr = ina;
        } else {
            addr.sin_addr.s_addr = INADDR_ANY;
        }
    }
    addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 32) < 0) {
        perror("listen");
        close(server_fd);
        return 1;
    }

    // --------------------------------------------------------
    //  Server startup message
    // --------------------------------------------------------
    printf("=====================================\n");
    printf("  Tic-Tac-Toe Server is running\n");
    printf("  Listening on %s:%d\n", cfg.bind_address, port);
    printf("=====================================\n\n");

    // --------------------------------------------------------
    //  Launch heartbeat thread
    // --------------------------------------------------------
    pthread_t hb;
    static int hb_limit;
    hb_limit = cfg.max_clients;
    if (hb_limit <= 0 || hb_limit > MAX_CLIENTS) hb_limit = MAX_CLIENTS;
    pthread_create(&hb, NULL, heartbeat_thread, &hb_limit);
    pthread_detach(hb);

    // --------------------------------------------------------
    //  Accept incoming connections
    // --------------------------------------------------------
    while (1) {
        struct sockaddr_in cliaddr;
        socklen_t clilen = sizeof(cliaddr);
        int cfd = accept(server_fd, (struct sockaddr*)&cliaddr, &clilen);
        if (cfd < 0) {
            perror("accept");
            continue;
        }

        struct Client* c = client_create(cfd);
        if (!c) {
            close(cfd);
            continue;
        }

        pthread_t th;
        if (pthread_create(&th, NULL, client_thread, c) != 0) {
            perror("pthread_create");
            client_destroy(c);
            continue;
        }

        pthread_detach(th);
        printf("[+] New client connected (fd=%d)\n", cfd);
    }

    // --------------------------------------------------------
    //  Cleanup (unreachable in normal operation)
    // --------------------------------------------------------
    close(server_fd);
    return 0;
}
