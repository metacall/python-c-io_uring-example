// This code is based on: https://github.com/shuveb/io_uring-by-example/blob/master/05_webserver_liburing/main.c

#include <stdio.h>
#include <netinet/in.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <liburing.h>
#include <sys/stat.h>
#include <fcntl.h>

#define SERVER_STRING		"Server: metacall/0.1\r\n"
#define QUEUE_DEPTH			256
#define READ_SZ				8192

#define EVENT_TYPE_ACCEPT	0
#define EVENT_TYPE_READ		1
#define EVENT_TYPE_WRITE	2

struct request {
	int event_type;
	int iovec_count;
	int client_socket;
	struct iovec iov[];
};

struct io_uring ring;

/*
 * Utility function to convert a string to lower case.
 * */

void strtolower(char *str) {
	for (; *str; ++str)
		*str = (char)tolower(*str);
}
/*
 One function that prints the system call and the error details
 and then exits with error code 1. Non-zero meaning things didn't go well.
 */
void fatal_error(const char *syscall) {
	perror(syscall);
	exit(1);
}

/*
 * Helper function for cleaner looking code.
 * */

void *zh_malloc(size_t size) {
	void *buf = malloc(size);
	if (!buf) {
		fprintf(stderr, "Fatal error: unable to allocate memory.\n");
		exit(1);
	}
	return buf;
}

/*
 * This function is responsible for setting up the main listening socket used by the
 * web server.
 * */

int setup_listening_socket(int port) {
	int sock;
	struct sockaddr_in srv_addr;

	sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock == -1)
		fatal_error("socket()");

	int enable = 1;
	if (setsockopt(sock,
				   SOL_SOCKET, SO_REUSEADDR,
				   &enable, sizeof(int)) < 0)
		fatal_error("setsockopt(SO_REUSEADDR)");


	memset(&srv_addr, 0, sizeof(srv_addr));
	srv_addr.sin_family = AF_INET;
	srv_addr.sin_port = htons(port);
	srv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

	/* We bind to a port and turn this socket into a listening
	 * socket.
	 * */
	if (bind(sock,
			 (const struct sockaddr *)&srv_addr,
			 sizeof(srv_addr)) < 0)
		fatal_error("bind()");

	if (listen(sock, 10) < 0)
		fatal_error("listen()");

	return (sock);
}

int add_accept_request(int server_socket, struct sockaddr_in *client_addr,
					   socklen_t *client_addr_len) {
	struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
	io_uring_prep_accept(sqe, server_socket, (struct sockaddr *) client_addr,
						 client_addr_len, 0);
	struct request *req = malloc(sizeof(*req));
	req->event_type = EVENT_TYPE_ACCEPT;
	io_uring_sqe_set_data(sqe, req);
	io_uring_submit(&ring);

	return 0;
}

int add_read_request(int client_socket) {
	struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
	struct request *req = malloc(sizeof(*req) + sizeof(struct iovec));
	req->iov[0].iov_base = malloc(READ_SZ);
	req->iov[0].iov_len = READ_SZ;
	req->event_type = EVENT_TYPE_READ;
	req->client_socket = client_socket;
	memset(req->iov[0].iov_base, 0, READ_SZ);
	/* Linux kernel 5.5 has support for readv, but not for recv() or read() */
	io_uring_prep_readv(sqe, client_socket, &req->iov[0], 1, 0);
	io_uring_sqe_set_data(sqe, req);
	io_uring_submit(&ring);
	return 0;
}

int add_write_request(struct request *req) {
	struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
	req->event_type = EVENT_TYPE_WRITE;
	io_uring_prep_writev(sqe, req->client_socket, req->iov, req->iovec_count, 0);
	io_uring_sqe_set_data(sqe, req);
	io_uring_submit(&ring);
	return 0;
}

/*
 * Sends the HTTP 200 OK header, the server string, for a few types of files, it can also
 * send the content type based on the file extension. It also sends the content length
 * header. Finally it send a '\r\n' in a line by itself signalling the end of headers
 * and the beginning of any content.
 * */

void send_headers(off_t data_len, struct iovec *iov) {
	char send_buffer[1024];

	static const char str[] = "HTTP/1.0 200 OK\r\n";
	unsigned long slen = sizeof(str) - 1;
	iov[0].iov_base = zh_malloc(slen);
	iov[0].iov_len = slen;
	memcpy(iov[0].iov_base, str, slen);

	slen = sizeof(SERVER_STRING) - 1;
	iov[1].iov_base = zh_malloc(slen);
	iov[1].iov_len = slen;
	memcpy(iov[1].iov_base, SERVER_STRING, slen);

	/* Send the Content-Type header */
	static const char content_type[] = "Content-Type: application/json\r\n";
	slen = sizeof(content_type) - 1;
	iov[2].iov_base = zh_malloc(slen);
	iov[2].iov_len = slen;
	memcpy(iov[2].iov_base, content_type, slen);

	/* Send the content-length header, which is the file size in this case. */
	sprintf(send_buffer, "content-length: %ld\r\n", data_len);
	slen = strlen(send_buffer);
	iov[3].iov_base = zh_malloc(slen);
	iov[3].iov_len = slen;
	memcpy(iov[3].iov_base, send_buffer, slen);

	/*
	 * When the browser sees a '\r\n' sequence in a line on its own,
	 * it understands there are no more headers. Content may follow.
	 * */
	static const char end_header[] = "\r\n";
	slen = sizeof(end_header) - 1;
	iov[4].iov_base = zh_malloc(slen);
	iov[4].iov_len = slen;
	memcpy(iov[4].iov_base, end_header, slen);
}

void handle_post_method(const char *path, const char *data, off_t len, int client_socket) {
	struct request *req = zh_malloc(sizeof(*req) + (sizeof(struct iovec) * 6));
	req->iovec_count = 6;
	req->client_socket = client_socket;
	send_headers(len, req->iov);
	req->iov[5].iov_base = zh_malloc(len);
	req->iov[5].iov_len = len;
	memcpy(req->iov[5].iov_base, data, len);
	add_write_request(req);
}

void handle_bad_request(int client_socket) {
	static const char bad_request[] = "HTTP/1.0 400 Bad Request\r\n\r\n";
	struct request *req = zh_malloc(sizeof(*req) + sizeof(struct iovec));
	unsigned long slen = sizeof(bad_request) - 1;
	req->iovec_count = 1;
	req->client_socket = client_socket;
	req->iov[0].iov_base = zh_malloc(slen);
	req->iov[0].iov_len = slen;
	memcpy(req->iov[0].iov_base, bad_request, slen);
	add_write_request(req);
}

/*
 * This function looks at method used and calls the appropriate handler function.
 * Since we only implement POST method, it calls handle_bad_request()
 * in case both these don't match. This sends an error to the client.
 * */

void handle_http_method(char *method_buffer, int client_socket, long (*handler)(long)) {
	char *method, *path, *saveptr;

	method = strtok_r(method_buffer, " ", &saveptr);
	strtolower(method);
	path = strtok_r(NULL, /*" "*/ " /", &saveptr);

	if (strcmp(method, "post") == 0) {
		long v = handler(strtol(path, NULL, 0));
		char str[128];
		size_t len = snprintf(str, 128, "%ld", v);
		handle_post_method(path, str, len, client_socket);
	} else {
		handle_bad_request(client_socket);
	}
}

int get_line(const char *src, char *dest, int dest_sz) {
	for (int i = 0; i < dest_sz; i++) {
		dest[i] = src[i];
		if (src[i] == '\r' && src[i+1] == '\n') {
			dest[i] = '\0';
			return 0;
		}
	}
	return 1;
}

int handle_client_request(struct request *req, long (*handler)(long)) {
	char http_request[1024];
	/* Get the first line, which will be the request */
	if(get_line(req->iov[0].iov_base, http_request, sizeof(http_request))) {
		fprintf(stderr, "Malformed request\n");
		exit(1);
	}
	handle_http_method(http_request, req->client_socket, handler);
	return 0;
}

void server_loop(int server_socket, long (*handler)(long)) {
	struct io_uring_cqe *cqe;
	struct sockaddr_in client_addr;
	socklen_t client_addr_len = sizeof(client_addr);

	add_accept_request(server_socket, &client_addr, &client_addr_len);

	while (1) {
		int ret = io_uring_wait_cqe(&ring, &cqe);
		if (ret < 0)
			fatal_error("io_uring_wait_cqe");
		struct request *req = (struct request *) cqe->user_data;
		if (cqe->res < 0) {
			fprintf(stderr, "Async request failed: %s for event: %d\n",
					strerror(-cqe->res), req->event_type);
			exit(1);
		}

		switch (req->event_type) {
			case EVENT_TYPE_ACCEPT:
				add_accept_request(server_socket, &client_addr, &client_addr_len);
				add_read_request(cqe->res);
				free(req);
				break;
			case EVENT_TYPE_READ:
				if (!cqe->res) {
					fprintf(stderr, "Empty request!\n");
					break;
				}
				handle_client_request(req, handler);
				free(req->iov[0].iov_base);
				free(req);
				break;
			case EVENT_TYPE_WRITE:
				for (int i = 0; i < req->iovec_count; i++) {
					free(req->iov[i].iov_base);
				}
				close(req->client_socket);
				free(req);
				break;
		}
		/* Mark this request as processed */
		io_uring_cqe_seen(&ring, cqe);
	}
}

void sigint_handler(int signo) {
	printf("^C pressed. Shutting down.\n");
	io_uring_queue_exit(&ring);
	exit(0);
}

int server_listen(int port, long (*handler)(long)) {
	int server_socket = setup_listening_socket(port);

	signal(SIGINT, sigint_handler);
	io_uring_queue_init(QUEUE_DEPTH, &ring, 0);
	server_loop(server_socket, handler);

	return 0;
}
