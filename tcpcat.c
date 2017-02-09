#if 0
(
set -eu
gcc -O2 -DSIMPLE_LOGGING -std=gnu11 -Wall -Wextra -Werror -Ic_modules -o tcpcat $(find -name '*.c') -lpthread
printf "%s\n" "GET www.google.com HTTP/1.1" "" | \
./tcpcat www.google.com 80

# valgrind --quiet --leak-check=full --track-origins=yes ./tcpcat www.google.com 80

# Can also use server/client:
stdbuf -o0 ./tcpcat -l1 :: 1234 -- tr [:lower:] [:upper:] &
uname -a | ./tcpcat :: 1234

)
exit 0
#endif
#include <cstd/std.h>
#include <cstd/unix.h>
#include <ctcp/socket.h>

static void set_cloexec(int fd)
{
	int prev = fcntl(fd, F_GETFD);
	if (prev == -1) {
		log_sysfail("fcntl", "");
	}
	if (fcntl(fd, F_SETFD, prev | O_CLOEXEC) == -1) {
		log_sysfail("fcntl", "");
	}
}

__attribute__((noreturn))
static void run_exec(struct socket_client *client, char **argv)
{
	if (dup2(client->fd, STDIN_FILENO) == -1) {
		log_sysfail("dup2", "");
		exit(1);
	}
	if (dup2(client->fd, STDOUT_FILENO) == -1) {
		log_sysfail("dup2", "");
		exit(1);
	}
	execvp(argv[0], argv);
	log_sysfail("execvp", "");
	exit(1);
}

static pid_t run_forkexec(struct socket_client *client, char **argv)
{
	pid_t pid = fork();
	if (pid == -1) {
		log_sysfail("fork", "");
		exit(1);
	} else if (pid == 0) {
		run_exec(client, argv);
		exit(1);
	}
	return pid;
}

static pid_t fork_cat(int in, int out)
{
	pid_t pid = fork();
	if (pid == -1) {
		log_sysfail("fork", "");
		exit(1);
	} else if (pid != 0) {
		return pid;
	}
	if (in != STDIN_FILENO) {
		if (dup2(in, STDIN_FILENO) == -1) {
			log_sysfail("dup2", "");
			exit (1);
		}
	}
	if (out != STDOUT_FILENO) {
		if (dup2(out, STDOUT_FILENO) == -1) {
			log_sysfail("dup2", "");
			exit (1);
		}
	}
	char *prog[] = {
		"/bin/cat",
		0
	};
	execvp(prog[0], prog);
	log_sysfail("execvp", "");
	exit(1);
}

__attribute__((noreturn))
static void run_stdio(struct socket_client *client)
{
	pid_t i, o;
	set_cloexec(client->fd);
	i = fork_cat(STDIN_FILENO, client->fd);
	o = fork_cat(client->fd, STDOUT_FILENO);

	waitpid(o, NULL, 0);

	kill(i, SIGTERM);

	waitpid(i, NULL, 0);

	exit(0);
}

static void show_help()
{
	fprintf(stderr, "tcpcat - because there are too many netcats and no one of them easily does everything that I want it to\n");
	fprintf(stderr, "Mark K Cowan, mark@open-cosmos.com\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "tcpcat -h\n");
	fprintf(stderr, "tcpcat [-s] <addr> <port> -- <command>...\n");
	fprintf(stderr, "tcpcat [-s] -l [-1] <addr> <port> -- <command>...\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "\t-s\tUse shell (%s) to execute command instead of exec\n", getenv("SHELL"));
	fprintf(stderr, "\t-l\tCreate server instead of client\n");
	fprintf(stderr, "\t-1\tServer handles one connection then exits\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "\tIn client-mode and with no command specified, tcpcat transmits from stdin and receives to stdout");
	fprintf(stderr, "\n");
	fprintf(stderr, "\tNOTE: When using -s, the command should be quoted as one single argument to tcpcat\n");
	fprintf(stderr, "\n");
}

static struct socket_server server;
static struct socket_client client;

static void close_server(int signo)
{
	(void) signo;
	socket_server_destroy(&server);
}

int main(int argc, char *argv[])
{
	bool listen = false;
	bool once = false;
	bool use_shell = false;
	int c;
	while ((c = getopt(argc, argv, "h1ls")) != -1) {
		switch (c) {
		case 'h': show_help(); return 0;
		case '1': once = true; break;
		case 'l': listen = true; break;
		case 's': use_shell = true; break;
		case '?': log_error("Invalid argument: '%c'", optopt); return 1;
		}
	}
	argc -= optind;
	argv += optind;
	if (argc < 2) {
		show_help();
		return 1;
	}
	struct fstr addr;
	struct fstr port;
	fstr_init_ref(&addr, argv[0]);
	fstr_init_ref(&port, argv[1]);
	argv += 2;
	argc -= 2;
	if (use_shell) {
		argc += 2;
		argv -= 2;
		argv[0] = getenv("SHELL");
		argv[1] = "-c";
	}
	if (listen) {
		if (!socket_server_init(&server, &addr, &port, NULL, NULL)) {
			log_error("Failed to start server");
			return 1;
		}
		if (signal(SIGINT, close_server) == SIG_ERR) {
			log_sysfail("signal", "");
		}
		if (signal(SIGTERM, close_server) == SIG_ERR) {
			log_sysfail("signal", "");
		}
		set_cloexec(server.fd);
		struct socket_client client;
		while (socket_server_accept(&server, &client)) {
			set_cloexec(client.fd);
			if (once) {
				socket_server_destroy(&server);
				run_exec(&client, argv);
			} else {
				run_forkexec(&client, argv);
			}
		}
		socket_server_destroy(&server);
		wait(NULL);
	} else {
		if (!socket_client_init(&client, &addr, &port, NULL)) {
			log_error("Failed to start client");
			return 1;
		}
		if (argc == 0) {
			run_stdio(&client);
		} else {
			run_exec(&client, argv);
		}
	}
	return 0;
}
