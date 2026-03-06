#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#include <sys/ioctl.h>

#include <linux/dvb/frontend.h>
#include <linux/dvb/dmx.h>

#include <stdbool.h>
#include <string.h>
#include <stdint.h>

#include <libzvbi.h>

#include <sys/stat.h> // Estos dos son para crear carpetas y cosas asi
#include <sys/types.h>


#define TAMANO_PAQUETE_TS 188

static vbi_decoder *dec = NULL;
static vbi_dvb_demux *demux = NULL;

static char *NOMBRE_CPT = NULL;




// PES a Sliced para zvbi
static vbi_bool pes_callback(vbi_dvb_demux *dx, void *user_data, const vbi_sliced *sliced, unsigned int lines, int64_t pts) {
	vbi_decoder *decoder = (vbi_decoder *) user_data;

    // Al PARECER el PTS se emite en 90khz, osea que el tiempo es eso entre / 90000(.0 por que tiene que ser float), esto es un hack, arreglar esto //
	double timestamp = pts / 90000.0;
	vbi_decode(decoder, sliced, lines, timestamp);
	return TRUE; // 1 si se rompe
}

// Extraemos el PID tal como lo estaba haciendo en mi otro programa, haciendo una mascara, moviendo 8 bits y añadiendo el tercer byte.
static uint16_t obtener_pid(const uint8_t *paquete) {
	return ((paquete[1] & 0x1F) << 8) | paquete[2];
}

// Cuando recibe una página nueva la manda aqui

static void recibir_pagina(vbi_event *ev, void *user_data) {
	if (ev->type != VBI_EVENT_TTX_PAGE)
		return;
	vbi_export *exportador;
	vbi_decoder *decoder = (vbi_decoder *) user_data;
	vbi_page pg;
	char *errstr = NULL;
	char subcarpeta[1024];
	char archivo[1024];
	if (vbi_fetch_vt_page(decoder, &pg, ev->ev.ttx_page.pgno, ev->ev.ttx_page.subno, VBI_WST_LEVEL_1p5, 25, 0)) {
		snprintf(subcarpeta, sizeof(subcarpeta), "%s-TELETEXTO/%03X", NOMBRE_CPT, pg.pgno);
		mkdir(subcarpeta, 0777);
		snprintf(archivo, sizeof(archivo), "%s/sub_%02X.html", subcarpeta, pg.subno);
		exportador = vbi_export_new("html", &errstr);
		if (exportador) {
			vbi_export_option_set(exportador, "full-html", "yes");
			vbi_export_option_set(exportador, "network", "1");
			vbi_export_option_set(exportador, "charset", "5");
			vbi_export_file(exportador, archivo, &pg);
			vbi_export_delete(exportador);
		}

		char archivo_raw[1024];
		snprintf(archivo_raw, sizeof(archivo_raw), "%s-TELETEXTO/%03X_%02X.raw", NOMBRE_CPT, pg.pgno, pg.subno);
		FILE *f = fopen(archivo_raw, "wb");
		if (f) {
			int dimensiones[2] = { pg.rows, pg.columns };
			fwrite(dimensiones, sizeof(int), 2, f);
			fwrite(pg.text, sizeof(vbi_char), pg.rows * pg.columns, f);
			fclose(f);
			printf("\r[+] Página %03X guardada en formato crudo: %s", pg.pgno, archivo_raw);
			fflush(stdout);
		}

		printf("\n--- PÁGINA TELETEXTO %03X ---\n", pg.pgno);
		char texto[4096];
		vbi_print_page_region(&pg, texto, sizeof(texto), "UTF-8", 0, 0, 0, 0, 40, 25);
		printf("%s\n", texto);
		vbi_unref_page(&pg);
	}
}


// Esto era mas que nada el programa antes, solo capturar, ahora tambien hacemos cosas con subtitulos y eso
int main(int argc, char *argv[]) {
	if (argc < 3) {
		printf("Uso: %s <frecuencia_en_Hz> <archivo>\n", argv[0]);
		printf("Ejemplo: %s 674000000 captura_dia_32_diciembre.ts\n", argv[0]);
		return 1;
	}

	NOMBRE_CPT = argv[2];

	long frecuencia_hz = atol(argv[1]); // EN ALGUN MOMENTO quiero que esto detecte si le faltan 0s, añadirlos, si no, pues nada.

	printf("Cambiando a frecuencia: %ld Hz...\n", frecuencia_hz);

	char carpeta[1024];
	snprintf(carpeta, sizeof(carpeta), "%s-TELETEXTO", argv[2]);
	bool error_carpeta = mkdir(carpeta, 0777);
	if (error_carpeta) {
		printf("Error creando carpeta!\n"); // Crea la carpeta para este stream
	}
    // El decoder de teletexto en si, se puede crear mas tarde, pero antes del bucle principal
	dec = vbi_decoder_new();
	if (!dec) {
		fprintf(stderr, "Error creando decoder VBI\n"); // En SE he leido que los 'profesionales' usan fprintf para enviar al stderr, igual tambien cambio lo de arriba
		return 1;
	}

	// Crear demux DVB (API correcta 0.2.44, si no te funciona... Mala suerte, busca en internet como compilar la .44)
    demux = vbi_dvb_pes_demux_new(pes_callback, dec);
	if (!demux) {
		fprintf(stderr, "Error creando demux DVB\n");
		return 1;
	}

	vbi_event_handler_add(dec, VBI_EVENT_TTX_PAGE, recibir_pagina, dec); // Basicamente una señal, conectar lo de recibir paginas a la... bueno, funcion de recibir paginas, duh.

	// A lo mejor tienes que cambiar esto
	int fe_fd  = open("/dev/dvb/adapter0/frontend1", O_RDWR);
	int dmx_fd = open("/dev/dvb/adapter0/demux1", O_RDWR);
	int dvr_fd = open("/dev/dvb/adapter0/dvr1", O_RDONLY);

	if (fe_fd < 0 || dmx_fd < 0 || dvr_fd < 0) {
		printf("Error abriendo el dispositivo! ¿Tienes permisos? (¿Grupo video o superusuario?)");
		return 1; // Lo que vendria a ser os.exit(1)
	}

	struct dtv_property props[] = { // El diccionario con las opciones para el IOCTL
		{ .cmd = DTV_DELIVERY_SYSTEM, .u.data = SYS_DVBT }, // Que estrucutra sigue, a que sistema va (DVB-T), DVBS o DVBS2 para satelite
		{ .cmd = DTV_FREQUENCY,       .u.data = (uint32_t) frecuencia_hz }, // Aqui va la frecuencia del argv 1 (0 es el programa en si)
		{ .cmd = DTV_BANDWIDTH_HZ,    .u.data = 8000000 }, // 8 MHz es algo 'generico' que deberia funcionar bien en España
		{ .cmd = DTV_TUNE } // Que lo ponga en esa frecuencia.
	};

	struct dtv_properties cmdseq = { .num = 4, .props = props }; // El 1/4 de seguridad de DVB, ni idea, lo he copiado de como lo hace VLC
	ioctl(fe_fd, FE_SET_PROPERTY, &cmdseq); // Enviar el comando al IOCTL del DVB

	struct dmx_pes_filter_params filter = {
		.pid = 0x2000, // Cualquier cosa (todo lo que venga por la antena)
		// ChatGPT me ha dicho que esto es malo, pero NO ME IMPORTA UNA PUTA MIERDA,
		// Es que no se coño explicar que necesito recibir TODO lo que venga de la antena!!!
		.input = DMX_IN_FRONTEND, // Usando el frontend que ya hemos seleccionado
		.output = DMX_OUT_TS_TAP, // Que la salida del demux vaya al dvr
		.pes_type = DMX_PES_OTHER, // Aceptar PESes con otros protocolos
		.flags = DMX_IMMEDIATE_START // No esperar al sigueinte paquete (Igual esto sobra)	
	};

	ioctl(dmx_fd, DMX_SET_PES_FILTER, &filter);
	// Puedes quitar esto, pero si lo quitas soltaras paquetes que no tengan headers PES, si haces esto, algunos muxes pierden datos
	// ACTUALIZACION: NO, NO PUEDES QUITAR ESTO.

	FILE *fp = fopen(argv[2], "wb");
	if (!fp) { // No fp significa que no puede abrir el archivo
		perror("Error abriendo archivo, tienes permisos?");
		return 1;
	}

	uint8_t buf[188 * 1024];
	uint16_t pid_objetivo = 4020;
	// = 230; // Esto cambia dependiendo de cada canal y incluso aleatoriamente en una misma emision, no se,

	printf("Iniciando captura...\n");

	while (1) {
        ssize_t r = read(dvr_fd, buf, sizeof(buf));
		if (r <= 0)
			continue;

		fwrite(buf, 1, r, fp);

		for (int i = 0; i < r; i += TAMANO_PAQUETE_TS) {
			uint8_t *paquete = &buf[i];
			if (paquete[0] != 0x47) // El 'header' de .TS
				continue;
			uint16_t pid = obtener_pid(paquete);
			if (pid != pid_objetivo) // Tengo que añadir un check de que solo devuelva el 'pid_objetivo' si es teletexto, si no un false. Por lo que he dicho antes de que esto puede cambiar aleatoriamente
				continue;
			// El adaptation field, segun la Wikipedia, esta al final del cuarto byte, que basicamente nos dice que si es 0 o 2 no hay que mover nada, si es 3 hay que mover segun el siguiente numero que recibe siguiente
			uint8_t afc = (paquete[3] >> 4) & 0x3;
			int offset = 4;
			if (afc == 0 || afc == 2)
				continue; // Entonces no hay teletexto aqui
			if (afc == 3) {
				uint8_t adaptation_length = paquete[4];
				offset += 1 + adaptation_length;
			}
			if (offset >= 188)
				continue;
			const uint8_t *payload = paquete + offset;
			unsigned int payload_len = 188 - offset;
			vbi_dvb_demux_feed(demux, payload, payload_len);
		}
	}
	return 0; // Salir bien, tengo que añadir lo de matar el demuxer cuando salgamos, aunque el watchdog deberia hacerlo solo. Whoops! He hecho un programa que no es memory-safe!
}
