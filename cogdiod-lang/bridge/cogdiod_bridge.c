/* ── Request handler ─────────────────────────────────────────────────────── */

static void handle(const char* req, char* resp, size_t resp_sz) {
    const char* op = json_op(req);
    pthread_mutex_lock(&store_lock);

    if (strcmp(op, "spawn") == 0) {
        Atom* a = alloc_atom();
        if (!a) { snprintf(resp, resp_sz, "{\"error\":\"full\"}"); goto done; }
        a->uuid = next_uuid++;
        json_str(req, "type", a->type, TYPE_LEN);
        json_str(req, "name", a->name, NAME_LEN);
        float s = json_float(req, "strength");
        float c = json_float(req, "confidence");
        if (s > 0) a->strength = s;
        if (c > 0) a->confidence = c;
        push_history(a);
        snprintf(resp, resp_sz, "{\"uuid\":%llu}", (unsigned long long)a->uuid);

    } else if (strcmp(op, "get_tv") == 0) {
        uint64_t uuid = json_u64(req, "uuid");
        Atom* a = find_atom(uuid);
        if (!a) { snprintf(resp, resp_sz, "{\"error\":\"not_found\"}"); goto done; }
        snprintf(resp, resp_sz, "{\"strength\":%.4f,\"confidence\":%.4f}",
                 a->strength, a->confidence);

    } else if (strcmp(op, "set_tv") == 0) {
        uint64_t uuid = json_u64(req, "uuid");
        Atom* a = find_atom(uuid);
        if (!a) { snprintf(resp, resp_sz, "{\"error\":\"not_found\"}"); goto done; }
        push_history(a);   /* save current TV before overwriting */
        float s = json_float(req, "strength");
        float c = json_float(req, "confidence");
        if (s >= 0) a->strength = s;
        if (c >= 0) a->confidence = c;
        snprintf(resp, resp_sz, "{\"ok\":true}");

    } else if (strcmp(op, "link") == 0) {
        uint64_t from = json_u64(req, "from");
        uint64_t to   = json_u64(req, "to");
        Atom* a = find_atom(from);
        if (!a || a->out_count >= MAX_LINKS) {
            snprintf(resp, resp_sz, "{\"error\":\"link_failed\"}"); goto done;
        }
        a->out_links[a->out_count++] = to;
        snprintf(resp, resp_sz, "{\"ok\":true}");

    } else if (strcmp(op, "unlink") == 0) {
        uint64_t from = json_u64(req, "from");
        uint64_t to   = json_u64(req, "to");
        Atom* a = find_atom(from);
        if (!a) { snprintf(resp, resp_sz, "{\"error\":\"not_found\"}"); goto done; }
        int removed = 0;
        for (int i = 0; i < a->out_count; i++) {
            if (a->out_links[i] == to) {
                /* Shift remaining links left */
                for (int j = i; j < a->out_count - 1; j++)
                    a->out_links[j] = a->out_links[j+1];
                a->out_count--;
                removed = 1;
                break;
            }
        }
        snprintf(resp, resp_sz, "{\"ok\":%s}", removed ? "true" : "false");

    } else if (strcmp(op, "destroy") == 0) {
        uint64_t uuid = json_u64(req, "uuid");
        Atom* a = find_atom(uuid);
        if (!a) { snprintf(resp, resp_sz, "{\"error\":\"not_found\"}"); goto done; }
        a->alive = 0;
        snprintf(resp, resp_sz, "{\"ok\":true}");

    } else if (strcmp(op, "get_links") == 0) {
        uint64_t uuid = json_u64(req, "uuid");
        Atom* a = find_atom(uuid);
        if (!a) { snprintf(resp, resp_sz, "{\"uuids\":[]}"); goto done; }
        char* p = resp;
        p += snprintf(p, resp_sz, "{\"uuids\":[");
        for (int i = 0; i < a->out_count; i++) {
            if (i) p += snprintf(p, resp_sz - (size_t)(p-resp), ",");
            p += snprintf(p, resp_sz - (size_t)(p-resp), "%llu",
                          (unsigned long long)a->out_links[i]);
        }
        snprintf(p, resp_sz - (size_t)(p-resp), "]}");

    } else if (strcmp(op, "get_sti") == 0) {
        uint64_t uuid = json_u64(req, "uuid");
        Atom* a = find_atom(uuid);
        if (!a) { snprintf(resp, resp_sz, "{\"error\":\"not_found\"}"); goto done; }
        snprintf(resp, resp_sz, "{\"sti\":%.4f}", a->sti);

    } else if (strcmp(op, "attend") == 0) {
        uint64_t uuid  = json_u64(req, "uuid");
        float    delta = json_float(req, "delta");
        Atom* a = find_atom(uuid);
        if (!a) { snprintf(resp, resp_sz, "{\"error\":\"not_found\"}"); goto done; }
        a->sti += delta;
        snprintf(resp, resp_sz, "{\"ok\":true,\"sti\":%.4f}", a->sti);

    } else if (strcmp(op, "snapshot") == 0) {
        char* p = resp;
        p += snprintf(p, resp_sz, "{\"atoms\":[");
        int first = 1;
        for (int i = 0; i < atom_count; i++) {
            if (!atoms[i].alive) continue;
            if (!first) p += snprintf(p, resp_sz-(size_t)(p-resp), ",");
            first = 0;
            p += snprintf(p, resp_sz-(size_t)(p-resp),
                "{\"uuid\":%llu,\"type\":\"%s\",\"name\":\"%s\","
                "\"strength\":%.4f,\"confidence\":%.4f,\"sti\":%.4f}",
                (unsigned long long)atoms[i].uuid,
                atoms[i].type, atoms[i].name,
                atoms[i].strength, atoms[i].confidence, atoms[i].sti);
        }
        snprintf(p, resp_sz-(size_t)(p-resp), "]}");

    } else if (strcmp(op, "stats") == 0) {
        float total_sti = 0;
        int   live = 0;
        for (int i = 0; i < atom_count; i++) {
            if (!atoms[i].alive) continue;
            total_sti += atoms[i].sti;
            live++;
        }
        snprintf(resp, resp_sz,
                 "{\"atom_count\":%d,\"total_sti\":%.4f}",
                 live, total_sti);

    } else if (strcmp(op, "pln_deduce") == 0) {
        /*
         * {"op":"pln_deduce","ant_uuid":1,"impl_uuid":3}
         * → deduces consequent TV from antecedent + implication TVs
         */
        uint64_t ant_uuid  = json_u64(req, "ant_uuid");
        uint64_t impl_uuid = json_u64(req, "impl_uuid");
        Atom* ant  = find_atom(ant_uuid);
        Atom* impl = find_atom(impl_uuid);
        if (!ant || !impl) {
            snprintf(resp, resp_sz, "{\"error\":\"not_found\"}"); goto done;
        }
        float s_out, c_out;
        pln_deduce_local(impl->strength, impl->confidence,
                         ant->strength,  ant->confidence,
                         &s_out, &c_out);
        snprintf(resp, resp_sz,
                 "{\"strength\":%.4f,\"confidence\":%.4f}", s_out, c_out);

    } else if (strcmp(op, "pln_revise") == 0) {
        /*
         * {"op":"pln_revise","uuid":1,"strength2":0.9,"confidence2":0.8}
         * → revises the atom's TV with a new observation
         */
        uint64_t uuid = json_u64(req, "uuid");
        Atom* a = find_atom(uuid);
        if (!a) { snprintf(resp, resp_sz, "{\"error\":\"not_found\"}"); goto done; }
        float s2 = json_float(req, "strength2");
        float c2 = json_float(req, "confidence2");
        float s_out, c_out;
        push_history(a);
        pln_revise_local(a->strength, a->confidence, s2, c2, &s_out, &c_out);
        a->strength   = s_out;
        a->confidence = c_out;
        snprintf(resp, resp_sz,
                 "{\"ok\":true,\"strength\":%.4f,\"confidence\":%.4f}",
                 s_out, c_out);

    } else if (strcmp(op, "episodic") == 0) {
        /*
         * {"op":"episodic","uuid":1,"version":2}
         * → returns the TV at version N (0=oldest, hist_count-1=newest before current)
         */
        uint64_t uuid    = json_u64(req, "uuid");
        int      version = (int)json_u64(req, "version");
        Atom* a = find_atom(uuid);
        if (!a) { snprintf(resp, resp_sz, "{\"error\":\"not_found\"}"); goto done; }
        if (a->hist_count == 0 || version < 0 || version >= a->hist_count) {
            snprintf(resp, resp_sz, "{\"error\":\"no_history\"}"); goto done;
        }
        /* Map version to ring buffer index.
         * version 0 = oldest = (hist_head - hist_count + TV_HIST_MAX) % TV_HIST_MAX
         * version hist_count-1 = newest saved = (hist_head - 1 + TV_HIST_MAX) % TV_HIST_MAX
         */
        int idx = (a->hist_head - a->hist_count + version + TV_HIST_MAX * 2) % TV_HIST_MAX;
        snprintf(resp, resp_sz,
                 "{\"version\":%d,\"strength\":%.4f,\"confidence\":%.4f}",
                 version, a->tv_hist_s[idx], a->tv_hist_c[idx]);

    } else if (strcmp(op, "rewrite_rule") == 0) {
        char from_type[TYPE_LEN];
        json_str(req, "from_type", from_type, TYPE_LEN);
        float new_s = json_float(req, "new_strength");
        float new_c = json_float(req, "new_confidence");
        float boost  = json_float(req, "boost");
        int applied = 0;
        for (int i = 0; i < atom_count; i++) {
            if (!atoms[i].alive) continue;
            if (strcmp(atoms[i].type, from_type) == 0) {
                push_history(&atoms[i]);
                if (boost > 0) {
                    atoms[i].strength = fminf(1.0f, atoms[i].strength * boost);
                } else {
                    if (new_s > 0) atoms[i].strength   = new_s;
                    if (new_c > 0) atoms[i].confidence = new_c;
                }
                applied++;
            }
        }
        snprintf(resp, resp_sz, "{\"ok\":true,\"applied\":%d}", applied);

    } else {
        snprintf(resp, resp_sz, "{\"error\":\"unknown_op\",\"op\":\"%s\"}", op);
    }

done:
    pthread_mutex_unlock(&store_lock);
}

/* ── Client thread ───────────────────────────────────────────────────────── */

static void* client_thread(void* arg) {
    int fd = *(int*)arg;
    free(arg);
    char req[BUF_SIZE], resp[BUF_SIZE];
    ssize_t n;
    while ((n = recv(fd, req, sizeof(req)-1, 0)) > 0) {
        req[n] = '\0';
        /* Strip trailing newline */
        char* nl = strchr(req, '\n');
        if (nl) *nl = '\0';
        if (strlen(req) == 0) continue;
        memset(resp, 0, sizeof(resp));
        handle(req, resp, sizeof(resp));
        /* Append newline delimiter */
        size_t rlen = strlen(resp);
        resp[rlen]   = '\n';
        resp[rlen+1] = '\0';
        send(fd, resp, rlen+1, 0);
    }
    close(fd);
    return NULL;
}

/* ── TCP accept thread (for Clojure and other JVM-based clients) ─────────── */

static void* tcp_accept_thread(void* arg) {
    int srv = *(int*)arg;
    free(arg);
    for (;;) {
        int* cfd = malloc(sizeof(int));
        *cfd = accept(srv, NULL, NULL);
        if (*cfd < 0) { free(cfd); break; }
        pthread_t t;
        pthread_create(&t, NULL, client_thread, cfd);
        pthread_detach(t);
    }
    return NULL;
}

/* ── Main ────────────────────────────────────────────────────────────────── */

int main(void) {
    /* UNIX socket */
    unlink(BRIDGE_SOCK_PATH);
    int srv = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, BRIDGE_SOCK_PATH, sizeof(addr.sun_path)-1);
    if (bind(srv, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind UNIX"); return 1;
    }
    listen(srv, 16);
    fprintf(stderr, "[cogdiod_bridge] UNIX socket: %s\n", BRIDGE_SOCK_PATH);

    /* TCP socket on port 19999 (for Clojure / JVM clients) */
    int tcp_srv = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_srv >= 0) {
        int opt = 1;
        setsockopt(tcp_srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        struct sockaddr_in tcp_addr = {0};
        tcp_addr.sin_family      = AF_INET;
        tcp_addr.sin_port        = htons(19999);
        tcp_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(tcp_srv, (struct sockaddr*)&tcp_addr, sizeof(tcp_addr)) == 0) {
            listen(tcp_srv, 16);
            fprintf(stderr, "[cogdiod_bridge] TCP socket: 127.0.0.1:19999\n");
            int* srv_ptr = malloc(sizeof(int));
            *srv_ptr = tcp_srv;
            pthread_t tcp_t;
            pthread_create(&tcp_t, NULL, tcp_accept_thread, srv_ptr);
            pthread_detach(tcp_t);
        } else {
            perror("bind TCP (non-fatal)");
            close(tcp_srv);
        }
    }

    /* UNIX accept loop (main thread) */
    for (;;) {
        int* cfd = malloc(sizeof(int));
        *cfd = accept(srv, NULL, NULL);
        if (*cfd < 0) { free(cfd); continue; }
        pthread_t t;
        pthread_create(&t, NULL, client_thread, cfd);
        pthread_detach(t);
    }
