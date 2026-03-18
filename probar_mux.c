#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <string.h> // Para borrar el array anterior cuando cargamos nuevos muxes
#include <linux/dvb/frontend.h>

#define ADAPTADOR "/dev/dvb/adapter0/frontend1"

int main(int argc, char *argv[]) {
	int fe_fd = open(ADAPTADOR, O_RDWR); // Copiado del codigo de capturar
	if (fe_fd < 0) {
		perror("No se pudo abrir el frontend. ¿Tienes permisos? (grupo 'video' en Debian)");
		return 1;
	}

	int mux;

	if (argc > 1) {
		mux = atoi(argv[1]);
		printf("Probado mux: %d\n", mux);
	} else {
		printf("Uso: %s (mux en hercios)\n", argv[0]);
		return 1;
	}


	struct dtv_property clear_prop[] = { { .cmd = DTV_CLEAR } };
	struct dtv_properties clear_cmd = { .num = 1, .props = clear_prop };
	ioctl(fe_fd, FE_SET_PROPERTY, &clear_cmd);

	struct dtv_property props[] = {
		{ .cmd = DTV_DELIVERY_SYSTEM, .u.data = SYS_DVBT },
		{ .cmd = DTV_FREQUENCY,       .u.data = mux },
		{ .cmd = DTV_MODULATION,      .u.data = QAM_AUTO },
		{ .cmd = DTV_BANDWIDTH_HZ,    .u.data = 8000000 },
		{ .cmd = DTV_INVERSION,       .u.data = INVERSION_AUTO },
		{ .cmd = DTV_TUNE }
	};
	struct dtv_properties cmdseq = { .num = 6, .props = props };

	if (ioctl(fe_fd, FE_SET_PROPERTY, &cmdseq) < 0) {
		fprintf(stderr, "Error al poner la antena con frecuencia %u Hz: ", mux);
		perror("");
		return 1;
	}

	fe_status_t estado; // Para no sobre cargar
	for (int i = 0; i < 15; i++) {
		usleep(100000);
		ioctl(fe_fd, FE_READ_STATUS, &estado);
		if (estado & FE_HAS_LOCK) {
			return 0;
		}
	}
	if (estado == 1) {
		return 1;
	}


}
