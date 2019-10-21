#include "BMP280.h"
#include "CCS811.h"
#include "HDC1080.h"

#define CLIENT_SERVER
#ifdef CLIENT_SERVER	// to select the application to build by this file

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <syslog.h>
#include <signal.h>
#include <getopt.h>
#include <string.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <errno.h>

/***************************************************************************/
/*  data definitions...                                                    */
/***************************************************************************/

#define SOCKET_FILE "/tmp/cjmcu-8128"
#define MEASURE_LOOP_INTERVAL	30	// in seconds (attention: too short will fail in CC811, should be >=20)
#define DISPLAY_LOOP_INTERVAL	MEASURE_LOOP_INTERVAL	// client output loop in case of option "-l"

enum commands_to_server {
	CMD_EXIT,
	CMD_GET_VALUES
};

struct command_to_server {
	uint8_t command;	// see enum commands_to_server
};

struct response_from_server {
	time_t server_start;// time of server start
	time_t time;		// time stamp of measurement time
	uint16_t co2;		// measured by CCS811
	uint16_t tvoc;		// measured by CCS811
	double humidity;	// measured by HDC1080
	double temp_HDC;	// measured by HDC1080
	double temp_BMP;	// measured by BMP280
	double pressure;	// measured by BMP280
};

struct cjmcu {
	CCS811 *ccs811;
    HDC1080 *hdc1080;
    BMP280 *bmp280;
};

static char *app_name = NULL;

/***************************************************************************/
/*  server functions...                                                    */
/***************************************************************************/


int init_response_data(struct response_from_server *rsp) {
	if (rsp == NULL) {
		syslog(LOG_ERR, "init_response_data(): parameter error");
		return -1;
	}
	memset(rsp, 0, sizeof(*rsp));
	rsp->time = rsp->server_start = time(NULL);
	return 0;
}

int measure(struct cjmcu *cjmcu, struct response_from_server *rsp) {
	if ((cjmcu == NULL) || (rsp == NULL)) {
		syslog(LOG_ERR, "measure(): parameter error");
		return -1;
	}
	if ((cjmcu->bmp280 == NULL) || (cjmcu->ccs811 == NULL) || (cjmcu->hdc1080 == NULL)) {
		syslog(LOG_ERR, "measure(): parameter error");
		return -1;
	}

	// trigger the measurement of the individual sensors:
    cjmcu->bmp280->measure();
    if (cjmcu->ccs811->read_sensors()) {
		syslog(LOG_WARNING, "[CC811] read sensors failed.");
	}
	if (cjmcu->hdc1080->measure()) {
		syslog(LOG_WARNING, "[HDC1080] read sensors failed.");
	}

	// get CC811 values:
	rsp->co2 = cjmcu->ccs811->get_co2();
	rsp->tvoc = cjmcu->ccs811->get_tvoc();
	// get BMP280 values:
	rsp->pressure = cjmcu->bmp280->get_pressure();
	rsp->temp_BMP = cjmcu->bmp280->get_temperature();
	// get HDC1080 values:
	rsp->humidity = cjmcu->hdc1080->get_recent_humidity();
	rsp->temp_HDC = cjmcu->hdc1080->get_recent_temperature();
	// timestamp of this measurement:
	rsp->time = time(NULL);

	cjmcu->ccs811->set_env_data(rsp->humidity, (rsp->temp_HDC + rsp->temp_BMP) / 2);

	return 0;
}

int create_server_socket() {
    int sock;
    struct sockaddr_un server;

    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
		syslog(LOG_ERR, "Unable to create socket: %s", strerror(errno));
        return -1;
    }
	unlink(SOCKET_FILE);
	memset(&server, 0, sizeof(server));
    server.sun_family = AF_UNIX;
    strncpy(server.sun_path, SOCKET_FILE, sizeof(server.sun_path)-1);
    if (bind(sock, (struct sockaddr *) &server, sizeof(struct sockaddr_un)) < 0) {
		syslog(LOG_ERR, "Unable to bind socket: %s", strerror(errno));
		close(sock);
        return -1;
    }
    if (listen(sock, 5) < 0) {
		syslog(LOG_ERR, "Unable to listen to socket: %s", strerror(errno));
		close(sock);
        return -1;
    }
    return sock;
}

int server_loop() {
    int ret, sock;
    struct pollfd fd;
	struct response_from_server current_values;
	struct command_to_server cmd;
	struct cjmcu device;
	int timeout = MEASURE_LOOP_INTERVAL * 1000;

    sock = create_server_socket();
    if (sock < 0) {		
        return -1;
    }

	// initialize the sensors:	
	syslog(LOG_INFO, "initialize sensors...");
	CCS811 ccs811("/dev/i2c-1", 0x5a);
    HDC1080 hdc1080("/dev/i2c-1", 0x40);
    BMP280 bmp280("/dev/i2c-1", 0x76);
	syslog(LOG_INFO, "sensors initialized...");

	device.ccs811 = &ccs811;
	device.hdc1080 = &hdc1080;
	device.bmp280 = &bmp280;

	init_response_data(&current_values);
    measure(&device, &current_values); // initial measurement

    fd.fd = sock; 
    fd.events = POLLIN;
    while (1) {
		int client_sock;
        ret = poll(&fd, 1, timeout);
        switch (ret) {
            case -1:	// error
				syslog(LOG_ERR, "poll failed: %s", strerror(errno));
			    close(sock);
    			return -1;
                break;

            case 0:		// timeout
                ret = measure(&device, &current_values);
				if (ret < 0) {
				    close(sock);
    				return -1;
				}
				timeout = MEASURE_LOOP_INTERVAL * 1000;
                break;

            default:	// data received from socket
				client_sock = accept(sock, NULL, NULL);
                ret = recv(client_sock , &cmd, sizeof(cmd), 0); // receive data from client
				if (ret < 0) {
					syslog(LOG_ERR, "recv failed: %s", strerror(errno));
				    close(client_sock);
					break;
				}
				if (ret != sizeof(cmd)) {
					syslog(LOG_ERR, "received invalid data size (%i/%lu)", ret, sizeof(cmd));
				} else {
					time_t difference;
					switch (cmd.command) {
						case CMD_EXIT:
							//syslog(LOG_INFO, "received EXIT command");
						    close(client_sock);
						    close(sock);
    						return 0;
							break;
						case CMD_GET_VALUES:
							//syslog(LOG_INFO, "received GET_VALUES command");
						    ret = send(client_sock, &current_values, sizeof(current_values), 0);
							if (ret < 0) {
								syslog(LOG_ERR, "send failed: %s", strerror(errno));
							}		
						    close(client_sock);	// support only one command in a connection!
							break;
						default:
							syslog(LOG_ERR, "received invalid command (%i)", cmd.command);
						    close(client_sock);
							break;
					}
					difference = time(NULL) - current_values.time;
					if (difference >= MEASURE_LOOP_INTERVAL) {
						ret = measure(&device, &current_values);
						if (ret < 0) {
						    close(sock);
    						return -1;
						}
						timeout = MEASURE_LOOP_INTERVAL * 1000;
					} else {
						timeout = (MEASURE_LOOP_INTERVAL - difference) * 1000;
					}
				}
                break;
        }
    }
    close(sock);
    return 0;
}

static void start_server()
{
	pid_t pid = 0;
	int fd, ret;

	pid = fork();
	if (pid < 0) {
		/* error */
		exit(EXIT_FAILURE);
	}

	/* Success: parent can continue */
	if (pid > 0) {
		return;
	}

	/* On success: The child process becomes session leader */
	if (setsid() < 0) {
		exit(EXIT_FAILURE);
	}

	/* Ignore signal sent from child to parent process */
	signal(SIGCHLD, SIG_IGN);

	/* Fork off for the second time*/
	pid = fork();
	if (pid < 0) {
		/* error */
		exit(EXIT_FAILURE);
	}

	/* Success: Let the parent terminate */
	if (pid > 0) {
		exit(EXIT_SUCCESS);
	}

	umask(0);
	chdir("/");

	/* Close all open file descriptors */
	for (fd = sysconf(_SC_OPEN_MAX); fd > 0; fd--) {
		close(fd);
	}

	/* Reopen stdin (fd = 0), stdout (fd = 1), stderr (fd = 2) */
	stdin = fopen("/dev/null", "r");
	stdout = fopen("/dev/null", "w+");
	stderr = fopen("/dev/null", "w+");

	/* Open system log and write message to it */
	openlog("cjmcu", LOG_PID|LOG_CONS, LOG_DAEMON);
	syslog(LOG_INFO, "Started %s", app_name);

	/* start server loop */
	ret = server_loop();

	/* server loop has terminated... */
	unlink(SOCKET_FILE);
	syslog(LOG_INFO, "Stopped %s", app_name);
	closelog();

	if (ret) {
		exit(EXIT_FAILURE);
	} else {
		exit(EXIT_SUCCESS);
	}
}

/***************************************************************************/
/*  client functions...                                                    */
/***************************************************************************/

int create_client_socket() {
    int sock;
    struct sockaddr_un server;

    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
		fprintf(stderr, "socket failed with code %i (%s)\n", errno, strerror(errno));
        return -1;
    }
	memset(&server, 0, sizeof(server));
    server.sun_family = AF_UNIX;
    strncpy(server.sun_path, SOCKET_FILE, sizeof(server.sun_path)-1);

    if (connect(sock, (struct sockaddr *) &server, sizeof(struct sockaddr_un)) < 0) {
		if ((errno != 2) && (errno != 111)) {
			fprintf(stderr, "connect failed with code %i (%s)\n", errno, strerror(errno));
		}
        close(sock);
        return -1;
    }
    return sock;
}

void print_help(void)
{
	printf("Usage: %s [OPTIONS]\n\n", app_name);
	printf("  Options:\n");
	printf("   -?			Print this help\n");
	printf("   -s			Stop/Terminate measurement daemon\n");
	printf("   -r			Reset/Restart measurement daemon\n");
	printf("   -p			Output air pressure value in hPa (taken from BMP200)\n");
	printf("   -t			Output temperature value in °C (taken from BMP200)\n");
	printf("   -T			Output temperature value in °C (taken from HDC1080)\n");
	printf("   -h			Output air humidity value in %% (taken from HDC1080)\n");
	printf("   -c			Output CO2 value in ppm (taken from CC811)\n");
	printf("   -o			Output TVOC value in ppb (taken from CC811)\n");
	printf("   -a			Output mean of temperature from BMP200 and HDC1080\n");
	printf("   -v			Output Summary of all available values\n");
	printf("   -l			Output Summary of all available values in a loop\n");
	printf("\n");
}

int client_loop(int sock, unsigned int loop_time) {
	struct command_to_server cmd;
	struct response_from_server rsp;

	cmd.command = CMD_GET_VALUES;

	while (1) {
		if (send(sock, &cmd, sizeof(cmd), 0) < 0) {
			fprintf(stderr, "send failed with code %i (%s)\n", errno, strerror(errno));
			return EXIT_FAILURE;
		}
		if (recv(sock, &rsp, sizeof(rsp), 0) < 0) {
			fprintf(stderr, "recv failed with code %i (%s)\n", errno, strerror(errno));
			return EXIT_FAILURE;
		}

		printf("T(HDC1080): %.2lf°C\tT(BMP280): %.2lf°C\tRH: %.2lf%%\tCO2: %uppm\tTVOC: %uppb\tPres: %.2lfhPa\n",
		rsp.temp_HDC, rsp.temp_BMP, rsp.humidity, rsp.co2, rsp.tvoc, rsp.pressure);

		close(sock);
		sleep(loop_time);
		if ((sock = create_client_socket()) < 0) {
			return EXIT_FAILURE;
		}
	}

	return EXIT_SUCCESS;
}

int client_run(int sock, int cmd_option) {
	struct command_to_server cmd;
	struct response_from_server rsp;
	int loop_time = DISPLAY_LOOP_INTERVAL;

	switch (cmd_option) {
		case 'p':
		case 't':
		case 'T':
		case 'h':
		case 'c':
		case 'o':
		case 'a':
		case 'v':
			cmd.command = CMD_GET_VALUES;
			if (send(sock, &cmd, sizeof(cmd), 0) < 0) {
				fprintf(stderr, "send failed with code %i (%s)\n", errno, strerror(errno));
				return EXIT_FAILURE;
			}
			if (recv(sock, &rsp, sizeof(rsp), 0) < 0) {
				fprintf(stderr, "recv failed with code %i (%s)\n", errno, strerror(errno));
				return EXIT_FAILURE;
			}
			break;

		case 's':	// stop daemon
			cmd.command = CMD_EXIT;
		    if (send(sock, &cmd, sizeof(cmd), 0) < 0) {
				fprintf(stderr, "send failed with code %i (%s)\n", errno, strerror(errno));
				return EXIT_FAILURE;
		    }
			return EXIT_SUCCESS;
			break;

		case 'L':	// output values in a loop
			loop_time = atoi(optarg);
			if (loop_time < 1) {
				loop_time = DISPLAY_LOOP_INTERVAL;
			}
		case 'l':	// output values in a loop
			return client_loop(sock, (unsigned int)loop_time);
			break;

		default:
			break;
	}

	switch (cmd_option) {
		case 'p':
			printf("%.2lf\n", rsp.pressure);
			return EXIT_SUCCESS;
			break;
		case 't':
			printf("%.2lf\n", rsp.temp_BMP);
			return EXIT_SUCCESS;
			break;
		case 'T':
			printf("%.2lf\n", rsp.temp_HDC);
			return EXIT_SUCCESS;
			break;
		case 'h':
			printf("%.2lf\n", rsp.humidity);
			return EXIT_SUCCESS;
			break;
		case 'c':
			printf("%u\n", rsp.co2);
			return EXIT_SUCCESS;
			break;
		case 'o':
			printf("%u\n", rsp.tvoc);
			return EXIT_SUCCESS;
			break;
		case 'a':
			printf("%.2lf\n", (rsp.temp_BMP + rsp.temp_HDC) / 2.0);
			return EXIT_SUCCESS;
			break;
		case 'v':
		    printf("Air Pressure:           %.2lf hPa\n", rsp.pressure);
		    printf("Temperature (BMP200):   %.2lf °C\n", rsp.temp_BMP);
		    printf("Temperature (HDC1080):  %.2lf °C\n", rsp.temp_HDC);
		    printf("Air Humidity:           %.2lf %%\n", rsp.humidity);
		    printf("CO2:                    %u ppm\n", rsp.co2);
		    printf("TVOC:                   %u ppb\n", rsp.tvoc);
		    printf("Age of the Values:      %li sec\n", time(NULL) - rsp.time);
		    printf("Uptime of server proc:  %li min\n", (time(NULL) - rsp.server_start) / 60);
			break;
		default:
			// should never happen...
		    fprintf(stderr, "unknown error...\n");
			break;
	}

	return EXIT_SUCCESS;
}

/***************************************************************************/
/*  main function                                                          */
/***************************************************************************/
int main(int argc, char *argv[])
{
	struct command_to_server cmd;
	struct response_from_server rsp;
	int cmd_option;
	int start_daemonized = 0;
	int retry_counter = 3;
	int ret, sock;

	app_name = argv[0];

	if (((cmd_option = getopt(argc, argv, "srptThcoavlL:?")) == -1) || (cmd_option == '?')) {
		print_help();
		return EXIT_FAILURE;
	}

    while ((sock = create_client_socket()) < 0) {
		// no server exists -> start one...
		start_server();
		sleep(1);

		if ((retry_counter--) == 0) {
			fprintf(stderr, "Unable to connect, giving up...\n");
			return EXIT_FAILURE;
		}
    }

	ret = client_run(sock, cmd_option);
    close(sock);

	return ret;
}

#else // CLIENT_SERVER
/***************************************************************************/
/*  this is a simpler main() function to test the interface                */
/***************************************************************************/
#include <iomanip>

int main() {
    CCS811 ccs811("/dev/i2c-1", 0x5a);
    HDC1080 hdc1080("/dev/i2c-1", 0x40);
    BMP280 bmp280("/dev/i2c-1", 0x76);

    while (true) {
        ccs811.read_sensors();
        bmp280.measure();

        if (hdc1080.measure() < 0) {
            std::cout << "Error on HDC1080 in measurement" << std::endl;
        }
        double t_bmp20 = bmp280.get_temperature();
        float t_hdc1080 = hdc1080.get_recent_temperature();
        float relative_humidity = hdc1080.get_recent_humidity();
        
        std::cout << "T(HDC1080): " << std::fixed << std::setprecision(2) << t_hdc1080 << "°C";
        std::cout << "\tT(BMP280): " << std::fixed << std::setprecision(2) << t_bmp20 << "°C";
        std::cout << "\tRH: " << std::fixed << std::setprecision(2) << relative_humidity << "%";
        std::cout << "\tCO2: " << std::dec << ccs811.get_co2() << "ppm";
        std::cout << "\tTVOC: " << std::dec << ccs811.get_tvoc() << "ppb";
        std::cout << "\tPres: " << std::fixed << std::setprecision(2) << bmp280.get_pressure() << "hPa";
        std::cout << std::endl;

        ccs811.set_env_data(relative_humidity, (t_hdc1080 + t_bmp20) / 2);

        std::this_thread::sleep_for(std::chrono::seconds(20));
    }
}

#endif // CLIENT_SERVER