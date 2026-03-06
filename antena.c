#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/dvb/frontend.h>
#include <linux/dvb/dmx.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>
#define ADAPTADOR "/dev/dvb/adapter0/frontend1"
#define DEMUX "/dev/dvb/adapter0/demux1"
int fe_fd;
int dmx_fd;
void salir(int sig) {
	close(fe_fd);
	printf("\r                                                                                                                             \nLiberando frontend...");
	// Esto es un poco 'hack', lo de "\r      \n" lo hago para borrar el ^C que sale al pulsar CTRL-C
	close(dmx_fd);
	printf("\nLiberando demuxer...");
	printf("\nEsperando durante un segundo... (Para no romper cosas...)\n");
	sleep(1);
	exit(0);
}
int main(int argc, char *argv[]) {
	signal(SIGINT, salir);
	if (argc < 2) {
		printf("Uso: %s <frecuencia_en_Hz>\n", argv[0]);
		printf("Ejemplo: %s 674000000\n", argv[0]);
		return 1;
	}
	long frecuencia_hz = atol(argv[1]);
	printf("Cambiando a frecuencia: %ld Hz...\n", frecuencia_hz);
	int fe_fd  = open(ADAPTADOR, O_RDWR);
	int dmx_fd = open(DEMUX, O_RDWR);
	if (fe_fd < 0 || dmx_fd < 0) {
		printf("Error abriendo el dispositivo! ¿Tienes permisos? (¿Grupo video o superusuario?)\n");
		return 1;
	}
	struct dtv_property props[] = { { .cmd = DTV_DELIVERY_SYSTEM, .u.data = SYS_DVBT }, { .cmd = DTV_FREQUENCY,       .u.data = (uint32_t) frecuencia_hz }, { .cmd = DTV_BANDWIDTH_HZ,    .u.data = 8000000 }, { .cmd = DTV_TUNE } };
	struct dtv_properties cmdseq = { .num = 4, .props = props };
	ioctl(fe_fd, FE_SET_PROPERTY, &cmdseq);
	struct dmx_pes_filter_params filter = { .pid = 0x2000, .input = DMX_IN_FRONTEND, .output = DMX_OUT_TS_TAP, .pes_type = DMX_PES_OTHER, .flags = DMX_IMMEDIATE_START};
	ioctl(dmx_fd, DMX_SET_PES_FILTER, &filter);
	printf("Haz CTRL-C para salir\n");
	while (1) { sleep(1);}
	salir(1);
}
