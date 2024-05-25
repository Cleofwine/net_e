

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <fcntl.h>
#include <pthread.h>

#include <sys/poll.h>
#include <sys/epoll.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sendfile.h>



#define BUFFER_LENGTH		1024
#define EVENT_LENGTH		512

#define KEY_MAX_LENGTH		128
#define VALUE_MAX_LENGTH	512
#define MAX_KEY_COUNT		128

typedef int (*ZVCALLBACK)(int fd, int events, void *arg);



typedef struct zv_connect_s {
	int fd;

	ZVCALLBACK cb;
	
	char rbuffer[BUFFER_LENGTH]; // channel
	int rc;
	int count;
	char wbuffer[BUFFER_LENGTH]; // channel
	int wc;


	char resource[BUFFER_LENGTH];
	int enable_sendfile;

	struct zv_kvstore_s *kvheader;
	
} zv_connect_t;

typedef struct zv_connblock_s {

	zv_connect_t *block;
	struct zv_connblock_s *next;

} zv_connblock_t;


typedef struct zv_reactor_s {

	int epfd;
	int blkcnt;
	
	zv_connblock_t *blockheader; //

} zv_reactor_t;

/// 
typedef struct zv_kvpair_s {

	char key[KEY_MAX_LENGTH]; 
	char value[KEY_MAX_LENGTH];
	
} zv_kvpair_t;

typedef struct zv_kvstore_s {

	struct zv_kvpair_s *table;
	int maxpairs;
	int numpairs;
	
} zv_kvstore_t;

// Connection

int init_kvpair(zv_kvstore_t *kvstore) {

	if (!kvstore) return -1;

	kvstore->table = calloc(MAX_KEY_COUNT, sizeof(zv_kvpair_t));
	if (!kvstore->table) return -2;

	kvstore->maxpairs = MAX_KEY_COUNT;
	kvstore->numpairs = 0;
	
	
}

void dest_kvpair(zv_kvstore_t *kvstore) {

	if (!kvstore) return ;
	if (!kvstore->table) {
		free(kvstore->table);
	}

}


int put_kvpair(zv_kvstore_t *kvstore, const char *key, const char *value) {

	if (!kvstore || !kvstore->table || !key || !value) return -1;

	// lock
	int idx = kvstore->numpairs ++;
	// unlock

	strncpy(kvstore->table[idx].key, key, KEY_MAX_LENGTH);
	strncpy(kvstore->table[idx].value, value, VALUE_MAX_LENGTH);

	

	return 0;
}

char * get_kvpair(zv_kvstore_t *kvstore, const char *key) {

	int i = 0;

	for (i = 0;i < kvstore->numpairs;i ++) {

		if (strcmp(kvstore->table[kvstore->numpairs].key, key) == 0) {
			return kvstore->table[kvstore->numpairs].value;
		}
		
	}

	return NULL;

}
//

int zv_init_reactor(zv_reactor_t *reactor) {

	if (!reactor) return -1;


	reactor->blockheader = malloc(sizeof(zv_connblock_t) + EVENT_LENGTH * sizeof(zv_connect_t));
	if (reactor->blockheader == NULL) return -1;

	reactor->blockheader->block = (zv_connect_t*)(reactor->blockheader + 1);
	reactor->blkcnt = 1;
	reactor->blockheader->next = NULL;

	reactor->epfd = epoll_create(1);



}


void zv_dest_reactor(zv_reactor_t *reactor) {

	if (!reactor) return ;

	if (!reactor->blockheader) free(reactor->blockheader);

	close(reactor->epfd);

}


int zv_connect_block(zv_reactor_t *reactor) {

	if (!reactor) return -1;

	zv_connblock_t *blk = reactor->blockheader;

	while (blk->next != NULL) blk = blk->next;

	// malloc block

	zv_connblock_t *connblock = malloc(sizeof(zv_connblock_t) + EVENT_LENGTH * sizeof(zv_connect_t));
	if (connblock == NULL) return -1;

	connblock->block = (zv_connect_t*)(connblock + 1);
	connblock->next = NULL;

	blk->next = connblock;
	reactor->blkcnt++;

	return 0;
}

zv_connect_t *zv_connect_idx(zv_reactor_t *reactor, int fd) {

	if (!reactor) return NULL;

	int blkidx = fd / EVENT_LENGTH;

	while (blkidx >= reactor->blkcnt) {
		zv_connect_block(reactor);
	}

	int i = 0;
	zv_connblock_t *blk = reactor->blockheader;
	while (i++ < blkidx) {
		blk = blk->next;
	}

	return &blk->block[fd % EVENT_LENGTH];
	
}

//recv_cb(reactor)
//send_cb()
// zv_connect_t

// GET / HTTP/1.1
// Host: 192.168.199.128:8000
// Connection: keep-alive
// Upgrade-Insecure-Requests: 1
// User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (
// KHTML, like Gecko) Chrome/112.0.0.0 Safari/537.36
// Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image
// /webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.7
// Accept-Encoding: gzip, deflate
// Accept-Language: zh-TW,zh;q=0.9,en-US;q=0.8,en;q=0.7

int readline(char *allbuf, int idx, char *linebuf) {

	int len = strlen(allbuf);

	for(;idx < len; idx ++) {
		if (allbuf[idx] == '\r' && allbuf[idx+1] == '\n') {
			return idx+2;
		} else {
			*(linebuf++) = allbuf[idx];
		}
	}
	return -1;
}

#define HTTP_WEB_ROOT		"/home/king/share/0voice2304/2-1-1-multi-io"


// http://192.168.199.128:8000/index.html

// sendfile

int zv_http_response(zv_connect_t *conn) {
#if 0
	int len = sprintf(conn->wbuffer, 
"HTTP/1.1 200 OK\r\n"
"Accept-Ranges: bytes\r\n"
"Content-Length: 78\r\n"
"Content-Type: text/html\r\n"
"Date: Sat, 06 Aug 2022 13:16:46 GMT\r\n\r\n"
"<html><head><title>0voice.king</title></head><body><h1>King</h1><body/></html>");

	conn->wc = len;
	
#elif 0

	printf("resources: %s\n", conn->resource);

	int filefd = open(conn->resource, O_RDONLY);
	if (filefd == -1) {
		printf("errno: %d\n", errno);
		return -1;
	}

	//fseek(, SEEK_END)
	struct stat stat_buf;
	fstat(filefd, &stat_buf);

	int len = sprintf(conn->wbuffer, 
"HTTP/1.1 200 OK\r\n"
"Accept-Ranges: bytes\r\n"
"Content-Length: %ld\r\n"
"Content-Type: text/html\r\n"
"Date: Sat, 06 Aug 2022 13:16:46 GMT\r\n\r\n", stat_buf.st_size);

	len += read(filefd, conn->wbuffer+len, BUFFER_LENGTH-len);
	
	conn->wc = len;

	close(filefd);

#elif 0

	int filefd = open(conn->resource, O_RDONLY);
	if (filefd == -1) {
		printf("errno: %d\n", errno);
		return -1;
	}

    struct stat stat_buf;
    fstat(filefd, &stat_buf);
    close(filefd);

    int len = sprintf(conn->wbuffer, 
"HTTP/1.1 200 OK\r\n"
"Accept-Ranges: bytes\r\n"
"Content-Length: %ld\r\n"
"Content-Type: text/html\r\n"
"Date: Sat, 06 Aug 2022 13:16:46 GMT\r\n\r\n", stat_buf.st_size);
	conn->wc = len;

	conn->enable_sendfile = 1;
	
#else

	int filefd = open(conn->resource, O_RDONLY);
	if (filefd == -1) {
		printf("errno: %d\n", errno);
		return -1;
	}

    struct stat stat_buf;
    fstat(filefd, &stat_buf);
    close(filefd);

    int len = sprintf(conn->wbuffer, 
"HTTP/1.1 200 OK\r\n"
"Accept-Ranges: bytes\r\n"
"Content-Length: %ld\r\n"
"Content-Type: image/png\r\n"
"Date: Sat, 06 Aug 2022 13:16:46 GMT\r\n\r\n", stat_buf.st_size);
	conn->wc = len;

	conn->enable_sendfile = 1;


#endif
	// conn->wbuffer
	// conn->wc = len

}

// request response 
// GET /0voice/king/skdfasdfasdfsa
int zv_http_request(zv_connect_t *conn) {

	// conn->rbuffer
	// conn->rc 

	printf("http --> request:\n %s\n", conn->rbuffer);

	char linebuffer[1024] = {0};
	int idx = readline(conn->rbuffer, 0, linebuffer);
	printf("line: %s\n", linebuffer);
	
#if 1
	if (strstr(linebuffer, "GET")) { // resource

		//printf("resource: %s\n", linebuffer+4);
		int i = 0;
		while (linebuffer[sizeof("GET ") + i] != ' ') i ++;
		linebuffer[sizeof("GET ") + i] = '\0';
		
		sprintf(conn->resource, "%s/%s", HTTP_WEB_ROOT, linebuffer + sizeof("GET "));
	
	} /*else {

		while (idx != -1) {
		
			idx = readline(conn->rbuffer, idx, linebuffer);

			// Accept-Ranges: bytes
			char *key = linebuffer;
			int i = 0;
			while (key[i++] != ':');
			key[i] = '\0';

			char *value = linebuffer+ i +1;

			//put_kvpair(conn->kvheader, key, value);
			
		}

	} */
#endif
	

}


int init_server(short port) {

	int sockfd = socket(AF_INET, SOCK_STREAM, 0); // io
	
	struct sockaddr_in servaddr;
	memset(&servaddr, 0, sizeof(struct sockaddr_in)); // 192.168.2.123
	servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY); // 0.0.0.0
    servaddr.sin_port = htons(port);

    if (-1 == bind(sockfd, (struct sockaddr*)&servaddr, sizeof(struct sockaddr))) {
		printf("bind failed: %s", strerror(errno));
		return -1;
    }

    listen(sockfd, 10); 

    printf("listen port: %d\n", port);

	return sockfd;
}

//
/*
EPOLLLT

 [51]
 fromId: Aid
 toId: Bid
 content: hello
 time: xxxxx

 */

int recv_cb(int fd, int event, void *arg);



int send_cb(int fd, int event, void *arg) {

	//printf("send_cb\n");

	zv_reactor_t *reactor = (zv_reactor_t*)arg;
	zv_connect_t *conn = zv_connect_idx(reactor, fd);


	zv_http_response(conn);
	

	//echo 
	send(fd, conn->wbuffer, conn->wc, 0);  // send header

#if 1
	if (conn->enable_sendfile) { // sendbody
	
		int filefd = open(conn->resource, O_RDONLY);
		if (filefd == -1) {
			printf("errno: %d\n", errno);
			return -1;
		}

		struct stat stat_buf;
		fstat(filefd, &stat_buf);
		
		int ret = sendfile(fd, filefd, NULL, stat_buf.st_size);  // sendbody
		if (ret == -1) {
			printf("errno: %d\n", errno);
		}

		close(filefd);

	}
	
#endif
	

	conn->cb = recv_cb;
	
	//
	struct epoll_event ev;
	ev.events = EPOLLIN; //
	ev.data.fd = fd;
	epoll_ctl(reactor->epfd, EPOLL_CTL_MOD, fd, &ev);

}


int recv_cb(int fd, int event, void *arg) {

	zv_reactor_t *reactor = (zv_reactor_t*)arg;
	zv_connect_t *conn = zv_connect_idx(reactor, fd);

	
	int ret = recv(fd, conn->rbuffer+conn->rc, conn->count, 0);
	if (ret < 0) {

	} else if (ret == 0) {

		//
		conn->fd = -1;
		conn->rc = 0;
		conn->wc = 0;

		//
		epoll_ctl(reactor->epfd, EPOLL_CTL_DEL, fd, NULL);
		
		close(fd);

		return -1;
	} else {
	
		conn->rc += ret;
		printf("rbuffer: %s, ret: %d\n", conn->rbuffer, conn->rc);

		// --> echo
		//memcpy(conn->wbuffer, conn->rbuffer, conn->rc);
		//conn->wc = conn->rc;

		zv_http_request(conn);

		conn->cb = send_cb;
		
		//
		struct epoll_event ev;
		ev.events = EPOLLOUT;
		ev.data.fd = fd;
		epoll_ctl(reactor->epfd, EPOLL_CTL_MOD, fd, &ev);
	

	}

}

int accept_cb(int fd, int events, void *arg) {

	struct sockaddr_in clientaddr;
    socklen_t len = sizeof(clientaddr);

	int clientfd = accept(fd, (struct sockaddr*)&clientaddr, &len);
	if (clientfd < 0) {
		printf("accept errno: %d\n", errno);
		return -1;
	}

	// 

	printf(" clientfd: %d\n", clientfd);

	zv_reactor_t *reactor = (zv_reactor_t*)arg;
	zv_connect_t *conn = zv_connect_idx(reactor, clientfd);

	conn->fd = clientfd;
	conn->cb = recv_cb;
	conn->count = BUFFER_LENGTH;
	//conn->kvheader = malloc(sizeof(zv_kvstore_t));
	//init_kvpair(conn->kvheader);
	
	struct epoll_event ev;
	ev.events = EPOLLIN;
	ev.data.fd = clientfd;
	epoll_ctl(reactor->epfd, EPOLL_CTL_ADD, clientfd, &ev);

}

int set_listener(zv_reactor_t *reactor, int fd, ZVCALLBACK cb) {

	if (!reactor || !reactor->blockheader) return -1;

	reactor->blockheader->block[fd].fd = fd;
	reactor->blockheader->block[fd].cb = cb;
	
	struct epoll_event ev;
	ev.events = EPOLLIN;
	ev.data.fd = fd;

	epoll_ctl(reactor->epfd, EPOLL_CTL_ADD, fd, &ev);
	
}


int main(int argc, char *argv[]) {
	//open

	if (argc < 2) return -1;

	
	
	//printf("sockfd: %d\n", sockfd);
	zv_reactor_t reactor;
	zv_init_reactor(&reactor);  // epoll 

	

	int port = atoi(argv[1]);

	int i = 0;
	for (i = 0;i < 1;i ++) {
		int sockfd = init_server(port+i);  //
		set_listener(&reactor, sockfd, accept_cb); // 
	}
	

	struct epoll_event events[EVENT_LENGTH] = {0};
	
	while (1) { //mainloop, event driver

		int nready = epoll_wait(reactor.epfd, events, EVENT_LENGTH, -1);

		int i = 0;
		for (i = 0;i < nready;i ++) {

			int connfd = events[i].data.fd;
			zv_connect_t *conn = zv_connect_idx(&reactor, connfd);

			if (events[i].events & EPOLLIN) {

				conn->cb(connfd, events[i].events, &reactor);

			}

			if (events[i].events & EPOLLOUT) {

				conn->cb(connfd, events[i].events, &reactor);

			}
		}

	}


}



