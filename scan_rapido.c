#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdint.h>
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
	for (int i = 0; i < 5; i++) {
		usleep(100000);
		ioctl(fd, FE_READ_STATUS, &estado);
		if (estado & FE_HAS_LOCK) {
			bloqueo = 1;
			break;
	        }
	}

	printf("%u MHz: %s\n", freq_hz , bloqueo ? "CONEXION" : "SIN RECEPCION"); // Con esto nos ahorramos un if
}

int main() {
	int fe_fd = open(ADAPTADOR, O_RDWR); // Copiado del codigo de capturar
	if (fe_fd < 0) {
		perror("No se pudo abrir el frontend. ¿Tienes permisos? (grupo 'video' en Debian)");
		return 1;
	}




	// Aqui queda rellenar todo esto para ver las frecuencias. De una lista, o algo. Como se hace con w_scan2
	int muxes[] = {
		570000000, // Este funciona donde estoy
		690000000,
		858000000
	};
	for (int frecuencia = 0; frecuencia < sizeof(muxes) / sizeof(muxes[0]); frecuencia++) {
		comprobar_mux(fe_fd, muxes[frecuencia]);
	}

	close(fe_fd);
	return 0;
}
