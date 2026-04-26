// ADVERTENCIA
// SOLO PROBADO CON DVB-S2
// PUEDE QUE FUNCIONE CON DVB-S(1)
// PERO NO PUEDO APUNTAR A NINGUN SATELITE CON DVB-S(1)

// sat2.c
// Un pequeño programita que apunta y guarda cosas del satelite.
// Nota: Esto usa 0x2000 para guardarlo TODO.
// Juan Pedro
// racista.es | 05012003.xyz

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/dvb/frontend.h>
#include <linux/dvb/dmx.h>
#include <string.h>
#include <errno.h>

#define ADAPTER       0
// Cambia esto por tu adaptador si, en mi caso es el 0, pero en mi caso tambien es frontend0
// TODO: Opcion en el programa en si para poner el adaptador


#define BATCH_SIZE    128

int main(int argc, char *argv[]) {
	char fe_path[64], dmx_path[64], dvr_path[64];
	sprintf(fe_path, "/dev/dvb/adapter%d/frontend0", ADAPTER);
	sprintf(dmx_path, "/dev/dvb/adapter%d/demux0", ADAPTER);
	sprintf(dvr_path, "/dev/dvb/adapter%d/dvr0", ADAPTER);

	if (argc < 4) {
		printf("Faltan argumentos!\n");
		printf("Uso: %s (frecuencia en kHz) (LNB bajo) (LNB switch) [Polarizacion: H/V] [Symbol rate]\n", argv[0]);
		printf("Ej: %s 11156000 9750000 11700000 V 22000000\n", argv[0]);
		return 1;
	}

	uint32_t freq = (uint32_t)atoll(argv[1]);
	uint32_t lnb_low = (uint32_t)atoll(argv[2]);
	uint32_t lnb_switch = (uint32_t)atoll(argv[3]);
	char polarization = (argc >= 5) ? argv[4][0] : 'V';
	uint32_t simbolos = (argc >= 6) ? (uint32_t)atoll(argv[5]) : 22000000;

	// Calcula que LNB usar
	// Si es mayor que el H usamos el V, es dificil de explicar, pero en resumen, le enviamos a la antena 13v o 18v
	uint32_t lnb = (freq >= lnb_switch) ? 10600000 : lnb_low;
	uint32_t fi = freq - lnb;

	printf("Sintonizando:\n");
	printf(" - Freq Sat: %u kHz\n", freq);
	printf(" - LNB usado: %u kHz\n", lnb);
	printf(" - Freq intermedia: %u kHz\n", fi);
	printf(" - Polarizacion: %c (Voltaje: %s)\n", polarization, (polarization == 'V') ? "13V" : "18V");
	printf(" - Simbolos: %u\n", simbolos);

	int fe_fd = open(fe_path, O_RDWR);
	if (fe_fd < 0) {
		perror("Error abriendo frontend");
		return 1;
	}

	// El nombre del frontend
	// (Reportado por el driver)
	struct dvb_frontend_info fe_info;
	if (ioctl(fe_fd, FE_GET_INFO, &fe_info) == 0) {
		printf("\nFrontend: %s\n", fe_info.name);
	}

	// Borrar ajustes anteriores
	// Me he encontrado con un bug donde VLC no limpia al salir anormalmente
	// Al menos no limpia la parte de DVB-S, por que el frontend aun estaba apuntado
	// Canal Sur A cuando probe cosas antes.
	printf("Reinicializando tuner...\n");
	struct dtv_property clear_props[] = {
		{ .cmd = DTV_CLEAR, .u.data = 0 }
	};
	struct dtv_properties clear_dtv = { .num = 1, .props = clear_props };
	ioctl(fe_fd, FE_SET_PROPERTY, &clear_dtv);
	usleep(100000);

	// Voltaje según polarización (esto controla la polarización en satélites)
	uint32_t voltage = (polarization == 'V') ? SEC_VOLTAGE_13 : SEC_VOLTAGE_18;

	printf("Configurando tuner...\n");
	struct dtv_property props[] = {
		{ .cmd = DTV_DELIVERY_SYSTEM, .u.data = SYS_DVBS },
		{ .cmd = DTV_FREQUENCY,       .u.data = fi },           // En kHz
		{ .cmd = DTV_SYMBOL_RATE,     .u.data = simbolos },
		{ .cmd = DTV_INNER_FEC,       .u.data = FEC_5_6 },      // FEC 5/6 como en VLC
		{ .cmd = DTV_VOLTAGE,         .u.data = voltage },      // Controla polarización y LNB
		{ .cmd = DTV_TONE,            .u.data = SEC_TONE_OFF }, // Est o puede ser tambien ON, pero en mi caso no he visto ningun canal que lo necesite (Astra 19.2E y Hispasat)
		{ .cmd = DTV_TUNE,            .u.data = 0 }
	};

	struct dtv_properties dtv_props = { .num = 7, .props = props };

	// Debug, me esta dando guerra, necesitaba mencionar obbligatoriamente el DTV_TONE
	if (ioctl(fe_fd, FE_SET_PROPERTY, &dtv_props) < 0) {
		printf("Error en FE_SET_PROPERTY: %s\n", strerror(errno));
		close(fe_fd);
		return 1;
	}

	printf("Configuración enviada OK\n");
	usleep(1000000);
	// Esperar 1 segundo
	// No es necesario si tienes una tarjeta buena, la mia es
	// "buena", pero aun asi no me mata esperarme un segundo
	// (Con solo unos milisegundos naturales de las cosas que
	// pasan aqui son suficientes para mi Hauppauge HVR WinTV4000)

	// Lee el estado
	fe_status_t status;
	printf("Esperando LOCK... ");
	fflush(stdout);


	// En resumen; Esperar 100 segundos hasta que consigamos lock, si no, salir.

	for (int i = 0; i < 100; i++) {
		ioctl(fe_fd, FE_READ_STATUS, &status);
		if (i % 10 == 0 || (status & FE_HAS_LOCK)) {
			printf("\n  [%2d] Status: 0x%x", i, status);
			if (status & FE_HAS_SIGNAL) printf(" [SIGNAL]");
			if (status & FE_HAS_CARRIER) printf(" [CARRIER]");
			if (status & FE_HAS_VITERBI) printf(" [VITERBI]");
			if (status & FE_HAS_SYNC) printf(" [SYNC]");
			if (status & FE_HAS_LOCK) printf(" [LOCK]");
			fflush(stdout);
		}
		if (status & FE_HAS_LOCK) {
			printf("\nLOCK conseguido!\n");
			break;
		}
		usleep(100000);
	}

	if (!(status & FE_HAS_LOCK)) {
		printf("\nNo se consiguió LOCK\n");
	}

	// EL DEMUXER (Que no usamos practicamente, ya que caputra todo)
	int dmx_fd = open(dmx_path, O_RDWR);
	if (dmx_fd < 0) {
		perror("Error abriendo demux");
		close(fe_fd);
		return 1;
	}

	// SI CAMBIAS EL 2000 SOLO CAPTURARAS LOS PIDS QUE PONGA
	struct dmx_pes_filter_params filter = {
		.pid      = 0x2000,
		.input    = DMX_IN_FRONTEND,
		.output   = DMX_OUT_TS_TAP,
		.pes_type = DMX_PES_OTHER,
		.flags    = DMX_IMMEDIATE_START
	};

	// Si te pasa esto estas muy jodido
	// Por que simplemente estas diciendo al demux que pase todo
	if (ioctl(dmx_fd, DMX_SET_PES_FILTER, &filter) < 0) {
		perror("Error en DMX_SET_PES_FILTER");
		close(dmx_fd);
		close(fe_fd);
		return 1;
	}

	// El DVR
	int dvr_fd = open(dvr_path, O_RDONLY);
	if (dvr_fd < 0) {
		perror("Error abriendo dvr");
		close(dmx_fd);
		close(fe_fd);
		return 1;
	}

	// TODO: Permitir cambiar el nombre
	FILE *f_out = fopen("captura.ts", "wb");
	if (!f_out) {
		perror("Error creando archivo de salida");
		close(dvr_fd);
		close(dmx_fd);
		close(fe_fd);
		return 1;
	}


	// 188*128 para .TS, puede ser mayor.
	uint8_t buffer[188 * BATCH_SIZE];
	printf("\nCapturando stream... (Ctrl+C para parar)\n");

	uint64_t bytes_capturados = 0;
	while (1) {
		ssize_t n = read(dvr_fd, buffer, sizeof(buffer));
		if (n > 0) {
			// Un pequeño bug que me he encontrado es que mientras este grabando el archivo esta 'corrupto', pero creo que no esta en RAM??
			// No entiendo el problema la verdad.
			fwrite(buffer, 1, n, f_out);
			bytes_capturados += n;
			// TODO: Si es mayor a 1024 MB, GB
			if (bytes_capturados % (188 * 1024 * 10) == 0) {
				printf("\r  Capturados: %llu MB", bytes_capturados / (1024*1024));
				fflush(stdout);
			}
		} else if (n < 0) {
			// Si se desconecta el adaptador repentinamente
			perror("Error leyendo DVR");
			break;
		}
	}


	// Esto sobra
	printf("\nCaptura finalizada: %llu bytes\n", bytes_capturados);

	// Cerrar limpio
	fclose(f_out);
	close(dvr_fd);
	close(dmx_fd);
	close(fe_fd);
	return 0;
}
