#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/dvb/frontend.h>
#include <linux/dvb/dmx.h> // Si solo usamos para hacer tune no nos hace falta, pero tambien capturamos los datos en su
#include <string.h>  // AHORA mismo no me hace falta, pero puede que pornto lo use
#include <stdlib.h>  // Texto a ulong
#include <stdint.h> // uint32_t

int main(int argc, char *argv[]) {

	if (argc < 2) {
		printf("Uso: %s <frecuencia_en_Hz>\n", argv[0]);
		printf("Ejemplo: %s 674000000\n", argv[0]); // Este es el de RTVA, que es por lo que escrito todo el codigo, por si se me olvida xd
		return 1;
	}
	long frecuencia_hz = atol(argv[1]); // LUEGO convertirlo a int32, que si no el IOCTL no funcionara, pero el compilador no se queja
	printf("Cambiando a frecuencia: %ld...\n", frecuencia_hz);
	int fe_fd  = open("/dev/dvb/adapter0/frontend1", O_RDWR);
	int dmx_fd = open("/dev/dvb/adapter0/demux1", O_RDWR);
	int dvr_fd = open("/dev/dvb/adapter0/dvr1", O_RDONLY);


	struct dtv_property props[] = {
		{ .cmd = DTV_DELIVERY_SYSTEM, .u.data = SYS_DVBT },
		{ .cmd = DTV_FREQUENCY,       .u.data = (uint32_t)frecuencia_hz }, // Aqui va la frecuencia del argv 1 (0 es el programa en si)
		{ .cmd = DTV_BANDWIDTH_HZ,    .u.data = 8000000 }, // 8 MHz es algo 'generico' que deberia funcionar bien en España
		{ .cmd = DTV_TUNE }
	};
	struct dtv_properties cmdseq = { .num = 4, .props = props };
	ioctl(fe_fd, FE_SET_PROPERTY, &cmdseq); // Enviar el comando al IOCTL del DVB

	struct dmx_pes_filter_params filter = {
		.pid = 0x2000, // Cualquier cosa (todo lo que venga por la antena)
		.input = DMX_IN_FRONTEND,
		.output = DMX_OUT_TS_TAP,
		.pes_type = DMX_PES_OTHER,
		.flags = DMX_IMMEDIATE_START
	};
	ioctl(dmx_fd, DMX_SET_PES_FILTER, &filter);

	FILE *fp = fopen("CAPTURA.ts", "wb");
	unsigned char buf[188 * 1024]; // Un paquete de .ts son 188 bytes (Mirar el archivo Explorar_Paquete188.py, ahi hago cosas con bits de Transport Stream y eso)
	printf("Iniciando captura... (Ctrl+C detiene la captura)\n");
	if (fe_fd < 0) {
		printf("Error abriendo el dispositivo! ¿Tienes permisos?");
		return 1;
	}
	while (1) {
		ssize_t r = read(dvr_fd, buf, sizeof(buf));
		if (r > 0) {
			fwrite(buf, 1, r, fp);
			printf("\rMegabytes: %.2f", (float)ftell(fp) / 1048576);
			fflush(stdout);
		} else {
			printf("\nFaltan datos! (Stream roto!)");
			fflush(stdout);
		}
	}
	return 0;
}
