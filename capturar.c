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

	if (argc < 3) {
		printf("Uso: %s <frecuencia_en_Hz> <archivo>\n", argv[0]);
		printf("Ejemplo: %s 674000000 captura_dia_32_diciembre.ts\n", argv[0]); // Este es el de RTVA, que es por lo que escrito todo el codigo, por si se me olvida xd
		return 1;
	}
	long frecuencia_hz = atol(argv[1]); // LUEGO convertirlo a int32, que si no el IOCTL no funcionara, pero el compilador no se queja
	printf("Cambiando a frecuencia: %ld...\n", frecuencia_hz);
	int fe_fd  = open("/dev/dvb/adapter0/frontend1", O_RDWR); // El frontened en si, CAMBIA ESTO PARA QUE SE AJUSTE AL TUYO!
	int dmx_fd = open("/dev/dvb/adapter0/demux1", O_RDWR); // El demuxer, esto sobra si no vas a tocarlo abajo (Quitar los paquetes que no tengan PES)
	int dvr_fd = open("/dev/dvb/adapter0/dvr1", O_RDONLY); // El grabador en si. Read Only por que no vamos a enviar nada ahi. Si quitas esto puedes capturar con otro programa, pero tambien tienes que cambiar abajo la logica de esperar mientras recibes datos.


	struct dtv_property props[] = { // El diccionario con las opciones para el IOCTL
		{ .cmd = DTV_DELIVERY_SYSTEM, .u.data = SYS_DVBT }, // Que estrucutra sigue, a que sistema va (DVB-T), DVBS o DVBS2 para satelite
		{ .cmd = DTV_FREQUENCY,       .u.data = (uint32_t)frecuencia_hz }, // Aqui va la frecuencia del argv 1 (0 es el programa en si)
		{ .cmd = DTV_BANDWIDTH_HZ,    .u.data = 8000000 }, // 8 MHz es algo 'generico' que deberia funcionar bien en España
		{ .cmd = DTV_TUNE } // Que lo ponga en esa frecuencia.
	};
	struct dtv_properties cmdseq = { .num = 4, .props = props }; // El 1/4 de seguridad de DVB, ni idea, lo he copiado de como lo hace VLC
	ioctl(fe_fd, FE_SET_PROPERTY, &cmdseq); // Enviar el comando al IOCTL del DVB

	struct dmx_pes_filter_params filter = {
		.pid = 0x2000, // Cualquier cosa (todo lo que venga por la antena)
		.input = DMX_IN_FRONTEND, // Usando el frontend que ya hemos seleccionado
		.output = DMX_OUT_TS_TAP, // Que la salida del demux vaya al dvr
		.pes_type = DMX_PES_OTHER, // Aceptar PESes con otros protocolos
		.flags = DMX_IMMEDIATE_START // No esperar al sigueinte paquete (Igual esto sobra)
	};
	ioctl(dmx_fd, DMX_SET_PES_FILTER, &filter);
	// Puedes quitar esto, pero si lo quitas soltaras paquetes que no tengan headers PES, si haces esto, algunos muxes pierden datos

	FILE *fp = fopen(argv[2], "wb"); // O el nombre del archivo que quieras capturar
	unsigned char buf[188 * 1024]; // Un paquete de .ts son 188 bytes (Mirar el archivo Explorar_Paquete188.py, ahi hago cosas con bits de Transport Stream y eso)
	printf("Iniciando captura... (Ctrl+C detiene la captura)\n");
	if (fe_fd < 0) { // Menor que cero es que no puede abrir el dispositivo
		printf("Error abriendo el dispositivo! ¿Tienes permisos? (¿Grupo video o superusuario?)");
		return 1; // Lo que vendria a ser os.exit(1)
	}
	while (1) {
		ssize_t r = read(dvr_fd, buf, sizeof(buf)); // El indicador de tamaño de los datos que hemos recibido, deberia ser 188*1024 (Osea, 1024 paquetes de Transport Stream)
		if (r > 0) { // Si hay bytes
			fwrite(buf, 1, r, fp); // Escribimos lo que hemos recibido en el archivo
			printf("\rMegabytes: %.2f", (float)ftell(fp) / 1048576); // Cuenta en tiempo real los Megabytes que llevamos
			// Aqui voy a añadir tambien un contador de tiempo
			fflush(stdout); // Limpiar la salida cada vez que se ejecute el while, que si no, el terminal se queda tonto.
		} else { // ESTO NO DEBERIA PASAR
			printf("\nFaltan datos! (Stream roto!)");
			fflush(stdout); // Por si cortamos justo cuando esta haciendo el fwrite anterior.
		}
	}
	return 0; // Salimos bien despues del while.
}
