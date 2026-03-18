#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <string.h> // Para borrar el array anterior cuando cargamos nuevos muxes
#include <linux/dvb/frontend.h>

#define ADAPTADOR "/dev/dvb/adapter0/frontend1"

void comprobar_mux(int fd, uint32_t freq_hz) {
	// LIMPIAR EL ESTADO (Lo necesito en mi tarjeta)
	struct dtv_property clear_prop[] = { { .cmd = DTV_CLEAR } };
	struct dtv_properties clear_cmd = { .num = 1, .props = clear_prop };
	ioctl(fd, FE_SET_PROPERTY, &clear_cmd); // Esto es lo que manda el comando en si

	// Los parametros para apuntar la antena
	// 8MHz para España es generalmente aceptable
	// Funciona en mi PC, pero vamos, que pruebes en la tuya. Puedes quitar todo menos bandwith y frequecny
	struct dtv_property props[] = {
		{ .cmd = DTV_DELIVERY_SYSTEM, .u.data = SYS_DVBT },
		{ .cmd = DTV_FREQUENCY,       .u.data = freq_hz },
		{ .cmd = DTV_MODULATION,      .u.data = QAM_AUTO },
		{ .cmd = DTV_BANDWIDTH_HZ,    .u.data = 8000000 },
		{ .cmd = DTV_INVERSION,       .u.data = INVERSION_AUTO },
		{ .cmd = DTV_TUNE }
	};
	struct dtv_properties cmdseq = { .num = 6, .props = props };

	if (ioctl(fd, FE_SET_PROPERTY, &cmdseq) < 0) {
		// Si falla, que nos diga el or que
		fprintf(stderr, "Error al poner la antena con frecuencia %u Hz: ", freq_hz);
		perror("");
		return;
	}

	fe_status_t estado; // Para no sobre cargar
	int bloqueo = 0;
	for (int i = 0; i < 15; i++) {
		usleep(100000);
		ioctl(fd, FE_READ_STATUS, &estado);
		if (estado & FE_HAS_LOCK) {
			bloqueo = 1;
			break;
	        }
	}

	printf("%u MHz: %s\n", freq_hz , bloqueo ? "CONEXION" : "SIN RECEPCION"); // Con esto nos ahorramos un if
}

int main(int argc, char *argv[]) {
	int fe_fd = open(ADAPTADOR, O_RDWR); // Copiado del codigo de capturar
	if (fe_fd < 0) {
		perror("No se pudo abrir el frontend. ¿Tienes permisos? (grupo 'video' en Debian)");
		return 1;
	}


	// Aqui queda rellenar todo esto para ver las frecuencias. De una lista, o algo. Como se hace con w_scan2
	int muxes[100] = {
		474000000, 482000000, 490000000, 498000000, 506000000, 514000000, 522000000, 530000000, 538000000, 546000000, 554000000, 562000000, 570000000, 578000000, 586000000, 594000000, 602000000, 610000000, 618000000, 626000000, 634000000, 642000000, 650000000, 658000000, 666000000, 674000000, 682000000, 690000000
	};
	int muxes_TOT = 27; // El numero total

	if (argc > 1) {
		FILE *pointer_archivo;
		printf("Cargando lista de muxes desde %s...", argv[1]);
		pointer_archivo = fopen(argv[1],"r");
		if (pointer_archivo == NULL) {
			printf("[ ERROR!! ]\n");
		} else {
			printf("[ OK ]\n");
			memset(muxes, 0, sizeof(muxes));
		}
		muxes_TOT  = 0;
		while (muxes_TOT < 100 && fscanf(pointer_archivo, "%d,", &muxes[muxes_TOT]) == 1) {
			muxes_TOT+=1;
		}
		for (int valor = 0; valor < muxes_TOT; valor+=1) {
			printf("MUX AGREGADO %d: %d\n", valor, muxes[valor]);
		}
	}


	printf("Comprobando %d muxes\n", muxes_TOT);
	for (int frecuencia = 0; frecuencia < muxes_TOT; frecuencia++) { // Hacerlo dinamico para cargar mas muxes de fuera
		comprobar_mux(fe_fd, muxes[frecuencia]);
	}

	close(fe_fd);
	return 0;
}
