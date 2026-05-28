// CAPTURAR V2
// CAPTURAR PERO CON MAS PES

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <wchar.h>

#include <sys/ioctl.h>

#include <linux/dvb/frontend.h>
#include <linux/dvb/dmx.h>

#include <stdbool.h>
#include <string.h>
#include <stdint.h>

#include <libzvbi.h>

#include <sys/stat.h>
#include <sys/types.h>


#define TAMANO_PAQUETE_TS 188
#define MAX_PID 8192
#define BITARRAY_SIZE ((MAX_PID + 7) / 8)
// Esto nunca se llego a implementar 'bien' en capturar.c


#define MAX_SLICES   64
#define PES_BUF_SIZE (128 * 1024)

int pid_ahora = 0;
int pagina_busqueda = -1;

static vbi_decoder *dec = NULL;
static vbi_sliced sliced[MAX_SLICES];

static char *NOMBRE_CPT = NULL;
static uint8_t pid_bitarray[BITARRAY_SIZE];

char carpeta[1024];

static int debug = 0;
static int packets_checked = 0;
static int packets_matched = 0;

// METEMOS AQUI LOS PIDS DE LOS PES QUE NO CONOCEMOS (OSEA LOS NO ESTANDARES (RTVE))
uint8_t *pes_bufs[MAX_PID] = {NULL};
int pes_lens[MAX_PID] = {0};
int pes_actives[MAX_PID] = {0};



// TODAS LAS FUNCIONES DE LOS PIDs
static inline void set_pid_bit(uint16_t pid) {
	if (pid < MAX_PID) {
		pid_bitarray[pid / 8] |= (1 << (pid % 8));
	}
}

static inline int is_pid_set(uint16_t pid) {
	if (pid < MAX_PID) {
		return (pid_bitarray[pid / 8] & (1 << (pid % 8))) != 0;
	}
	return 0;
}

// Identificadores de teletexto (Permisivo para incluir 0x21 de RTVE)
static int is_teletext_data_identifier(uint8_t id) {
	return (id >= 0x10 && id <= 0x1F) ||
	       (id >= 0x20 && id <= 0x9B) || 
	       (id >= 0x99 && id <= 0x9B);
}

static int is_teletext_data_unit(uint8_t id) {
	return id == 0x02 || id == 0x03;
}


// TS a VBI_SLICED (Igual que FFMPEG)
static int slice_to_vbi_lines(const uint8_t *buf, int size) { // EL BUFFER TIENE QUE SER EL PAQUETE ENTERO. NO METER AQUI EL PAQUETE YA SLICEADO
	int lines = 0;

	while (size >= 2 && lines < MAX_SLICES) {
		int data_unit_id     = buf[0];
		int data_unit_length = buf[1];

		if (data_unit_length + 2 > size)
			break; // Como minimo ya de por si hemos quitado 2 bytes, si no es asi, lo hemos roto, salir de aqui

		if (is_teletext_data_unit(data_unit_id)) { // Igual que en FFMPEG, si el principio 'parece' teletexto lo aceptamos como teletexto. PERO libzvbi es mas listo que yo, asi que si incluso se le pasa un paquete malo, no pasa nada (O no deberia)
			if (data_unit_length != 0x2c) {
				size -= data_unit_length + 2;
				buf  += data_unit_length + 2;
				continue;
			}

			int line_offset  = buf[2] & 0x1f;
			int field_parity = buf[2] & 0x20;

			sliced[lines].id   = VBI_SLICED_TELETEXT_B;
			sliced[lines].line = (line_offset > 0)
			                     ? (line_offset + (field_parity ? 0 : 313))
			                     : 0;

			for (int i = 0; i < 42; i++)
				sliced[lines].data[i] = vbi_rev8(buf[4 + i]);

			lines++;
		}

		size -= data_unit_length + 2;
		buf  += data_unit_length + 2;
	}

	return lines;
}

// EL PROCESADOR DE PES, EL PES TIENE QUE TENER AL MENOS 9 BITS, SI NO ES ASI, ESTA ROTO!!!
static void process_pes(const uint8_t *pes, int len, uint16_t pid) {
	if (len < 9) return;
	if (pes[0] != 0x00 || pes[1] != 0x00 || pes[2] != 0x01) {
		if (debug) fprintf(stderr, "PES %04X: Error de cabecera ( ! 000001 )\n", pid);
		return;
	}

	// pes[8] es el PES_header_data_length segun el estandar
	int pes_header_data_length = pes[8];
	int payload_start  = 9 + pes_header_data_length;

	if (payload_start >= len) { // PAQUETE ROTO, MAS GRANDE QUE EL LEN DEL PROPIO PAQUETE (Dividido ???? me encuentro esto mucho en Atresmedia??)
		if (debug) fprintf(stderr, "PES %04X: payload_start (%d) excede len (%d)\n", pid, payload_start, len);
		return;
	}

	uint8_t data_id = pes[payload_start];

	if (!is_teletext_data_identifier(data_id)) { // Esto spamea muchisimo si tienes una antena mal apuntada (Como la mia :D)
		// if (debug) fprintf(stderr, "PES %04X: ID Desconocido descartado: 0x%02X\n", pid, data_id);
		return;
	}

	const uint8_t *data = pes + payload_start + 1;
	int size = len - payload_start - 1;

	int lines = slice_to_vbi_lines(data, size);

	if (lines > 0) { // Tenemos el PES!!
		pid_ahora = pid;
		vbi_decode(dec, sliced, lines, 0.0);
	} else if (debug) {
		fprintf(stderr, "PES %04X: 0 lineas decodificadas\n", pid);
	}
}

// Copiado de SO, probablemente cambiare esto en el futuro por simplemente un argc[4] o incluso leer yo mismo el PMT, que no creo que sea muy dificil
// Parcialmente re-escrito por Claude
static int load_pids_from_json(const char *json_file) {
	FILE *f = fopen(json_file, "r");
	if (!f) {
		fprintf(stderr, "Error: No se puede abrir %s\n", json_file);
		return 0;
	}

	char buffer[65536];
	size_t bytes_read = fread(buffer, 1, sizeof(buffer) - 1, f);
	fclose(f);

	if (bytes_read <= 0) {
		fprintf(stderr, "Error: El archivo JSON está vacío\n");
		return 0;
	}

	buffer[bytes_read] = '\0';

	const char *pids_start = strstr(buffer, "\"teletext_pids\"");
	if (!pids_start) {
		fprintf(stderr, "Error: No se encontró 'teletext_pids' en el JSON\n");
		return 0;
	}

	const char *bracket = strchr(pids_start, '['); // Mas estricto??
	if (!bracket) {
		fprintf(stderr, "Error: Formato JSON inválido\n");
		return 0;
	}

	int pid_count = 0;
	const char *p = bracket + 1;

	while (*p && *p != ']') {
		while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ',')) {
			p++;
		}

		if (*p >= '0' && *p <= '9') {
			long pid = strtol(p, (char **)&p, 10);
			if (pid >= 0 && pid < MAX_PID) {
				set_pid_bit((uint16_t)pid);
				if (!pes_bufs[pid]) {
					pes_bufs[pid] = malloc(PES_BUF_SIZE);
				}
				pid_count++;
				printf("  PID cargado y buffer asignado: %ld (0x%04lX)\n", pid, pid);
			}
		} else if (*p == ']') {
			break;
		} else {
			p++;
		}
	}

	printf("Total de %d PIDs de teletexto cargados\n", pid_count);
	return pid_count;
}

// Esto es lo basico, lo de paquete a PID del anterior.
static uint16_t obtener_pid(const uint8_t *paquete) {
	return ((paquete[1] & 0x1F) << 8) | paquete[2];
}

// Uc a UTF8
static void write_unicode_char(FILE *f, uint32_t unicode) {
	if (unicode == 0) {
		fprintf(f, " ");
		return;
	}
	// Especiales
	switch (unicode) {
		case '"':  fprintf(f, "\\\""); return;
		case '\\': fprintf(f, "\\\\"); return;
		case '\b': fprintf(f, "\\b");  return;
		case '\f': fprintf(f, "\\f");  return;
		case '\n': fprintf(f, "\\n");  return;
		case '\r': fprintf(f, "\\r");  return;
		case '\t': fprintf(f, "\\t");  return;
	}

	// Si es menos de 0x20, usamos los escapes (Esto se ve mejor en el script en si
	if ((unsigned char)unicode < 0x20) {
		fprintf(f, "\\u%04x", unicode);
		return;
	}

	// UTF8 normal y corriente
	if (unicode < 0x80) {
		fprintf(f, "%c", (char)unicode);
	} else if (unicode < 0x800) {
		fprintf(f, "%c%c",
			0xC0 | (unicode >> 6),
			0x80 | (unicode & 0x3F));
	} else if (unicode < 0x10000) {
		fprintf(f, "%c%c%c",
			0xE0 | (unicode >> 12),
			0x80 | ((unicode >> 6) & 0x3F),
			0x80 | (unicode & 0x3F));
	} else if (unicode < 0x110000) {
		fprintf(f, "%c%c%c%c",
			0xF0 | (unicode >> 18),
			0x80 | ((unicode >> 12) & 0x3F),
			0x80 | ((unicode >> 6) & 0x3F),
			0x80 | (unicode & 0x3F));
	}
	// De internet, caracteres de mas de un byte. (No deberia ver esto en Teletexto en España, pero por si acaso)
}

// Cuando recibe una pagina (Corregido)
static void recibir_pagina(vbi_event *ev, void *user_data) {
	char subcarp[2048];
	snprintf(subcarp, sizeof(subcarp), "TELETEXTO/%s/PID%d", NOMBRE_CPT, pid_ahora);
	mkdir(subcarp, 0777);

	if (ev->type != VBI_EVENT_TTX_PAGE) return;
	vbi_decoder *decoder = (vbi_decoder *) user_data;
	vbi_page pg;

	if (vbi_fetch_vt_page(decoder, &pg, ev->ev.ttx_page.pgno, ev->ev.ttx_page.subno, VBI_WST_LEVEL_3p5, 25, 0)) { // Seria interesante poner el nivel de txto en runtime

		// ESTO ERA MUY IMPORTANTE PARA EL BLEEDING
		if (pg.dirty.y0 != 0 || pg.dirty.y1 < pg.rows - 1) {
			vbi_unref_page(&pg);
			return;
		}

		if (pagina_busqueda >= 0 && pg.pgno != pagina_busqueda) {
			vbi_unref_page(&pg);
			return;
		}

		printf("\rEncontrado bloque teletexto: %03X-%02X del PID %d", pg.pgno, pg.subno, pid_ahora);
		char filename[1024];
		snprintf(filename, sizeof(filename), "%s/%03X-%02X.json", subcarp, pg.pgno, pg.subno);
		FILE *f = fopen(filename, "w");
		if (f) {
			fprintf(f, "{\n");
			fprintf(f, "  \"pagina\": \"%03X\",\n", pg.pgno);
			fprintf(f, "  \"subpagina\": \"%02X\",\n", pg.subno);
			fprintf(f, "  \"filas\": %d,\n", pg.rows);
			fprintf(f, "  \"columnas\": %d,\n", pg.columns);
			fprintf(f, "  \"contenido_texto\": [\n");

			for (int r = 0; r < pg.rows; r++) {
				fprintf(f, "    \"");
				for (int c = 0; c < pg.columns; c++) {
					vbi_char vc = pg.text[r * pg.columns + c];
					write_unicode_char(f, vc.unicode);
				}
				fprintf(f, "\"%s\n", (r == pg.rows - 1) ? "" : ",");
			}

			fprintf(f, "  ],\n");
			fprintf(f, "  \"contenido_detallado\": [\n");

			for (int r = 0; r < pg.rows; r++) {
				fprintf(f, "    [\n");
				for (int c = 0; c < pg.columns; c++) {
					vbi_char vc = pg.text[r * pg.columns + c];
					fprintf(f, "      {\n");
					fprintf(f, "        \"unicode\": %u,\n", vc.unicode);
					fprintf(f, "        \"caracter\": \"");
					write_unicode_char(f, vc.unicode);
					fprintf(f, "\",\n");
					fprintf(f, "        \"foreground\": %u,\n", vc.foreground);
					fprintf(f, "        \"background\": %u,\n", vc.background);
					fprintf(f, "        \"opacity\": %u,\n", vc.opacity);
					fprintf(f, "        \"size\": %u,\n", vc.size);
					fprintf(f, "        \"underline\": %u,\n", vc.underline);
					fprintf(f, "        \"bold\": %u,\n", vc.bold);
					fprintf(f, "        \"italic\": %u,\n", vc.italic);
					fprintf(f, "        \"flash\": %u,\n", vc.flash);
					fprintf(f, "        \"conceal\": %u\n", vc.conceal);
					fprintf(f, "      }%s\n", (c == pg.columns - 1) ? "" : ",");
				}
				fprintf(f, "    ]%s\n", (r == pg.rows - 1) ? "" : ",");
			}

			fprintf(f, "  ]\n");
			fprintf(f, "}\n");
			fclose(f);
		}
		vbi_unref_page(&pg);
		// Antes de cerrar podriamos meter un lineas_dobleL o lineas_dobleH
		// Para poder implementar lo de las lineas de doble altura y anchura
	}
}


void licencia(char *a) {
	printf("\n\n\t\t%s - Programa de pre-procesado y captura de streams de DVB-T enfocado en teletexto\n", a);
	printf("\t\t%s es software libre: puedes redistribuirlo y/o modificarlo bajo los terminos de la\n", a);
	printf("\t\tGPL de GNU, bien bajo la licencia V3 (o a tu discrecion, cualquier version superior)\n");
	printf("\t\tESTE PROGRAMA ESTA DISTRIBUIDO CON LA IDEA DE SER UTIL; PERO SIN NINGUNA GARANTIA; puedes\n");
	printf("\t\tleer el la licencia GPL de GNU en https://www.gnu.org/licenses/\n\n\n", a);
}

void mostrar_ayuda(char *a) {
	printf("Uso: %s <frecuencia_en_Hz> <archivo> <archivo_pids.json> [debug]\n", a);
	printf("Ejemplo: %s 674000000 captura_dia_32_diciembre.ts pids.json\n", a);
	printf("Debug: %s 674000000 captura_dia_32_diciembre.ts pids.json debug\n", a);
}


int main(int argc, char *argv[]) {
	licencia(argv[0]); // Añadir algo como lo de -hide_banner de ffmpeg
	if (argc < 4) {
		mostrar_ayuda(argv[0]);
		return 1;
	}

	NOMBRE_CPT = argv[2];
	if (argc > 4 && strcmp(argv[4], "debug") == 0) {
		debug = 1;
		printf("Modo DEBUG activado\n"); // Muy posible que no veas esto, pasa muy rapido
	}

	long frecuencia_hz = atol(argv[1]);

	printf("Cambiando a frecuencia: %ld Hz...\n", frecuencia_hz);

	memset(pid_bitarray, 0, BITARRAY_SIZE); // Me da miedo usar memset

	printf("Cargando PIDs de teletexto desde %s...\n", argv[3]);
	if (load_pids_from_json(argv[3]) == 0) {
		fprintf(stderr, "Error: No se pudieron cargar los PIDs\n");
		return 1;
	}

	char carp_base[10];
	snprintf(carp_base, sizeof(carp_base), "TELETEXTOS");
	if (mkdir(carp_base, 0777) && errno != EEXIST) {
		printf("Error creando la carpeta contenedora!");
		return 1;
	}

	char carp_restos_streams[8];
	snprintf(carp_restos_streams, sizeof(carp_restos_streams), "STREAMS");
	if (mkdir(carp_restos_streams, 0777) && errno != EEXIST) {
		printf("Error creando la carpeta contenedora!");
		return 1;
	}

	snprintf(carpeta, sizeof(carpeta), "TELETEXTO/%s", argv[2]);
	if (mkdir(carpeta, 0777) && errno != EEXIST) {
		printf("Error creando carpeta!\n");
	}

	dec = vbi_decoder_new();
	if (!dec) {
		fprintf(stderr, "Error creando decoder VBI\n (Tienes libzvbi?)\n");
		return 1;
	}

	vbi_event_handler_add(dec, VBI_EVENT_TTX_PAGE, recibir_pagina, dec);

	int fe_fd  = open("/dev/dvb/adapter0/frontend1", O_RDWR); // CAMBIAR ESTO
	int dmx_fd = open("/dev/dvb/adapter0/demux1", O_RDWR); // Y PONERLO SEGUN
	int dvr_fd = open("/dev/dvb/adapter0/dvr1", O_RDONLY); // TUS NECESIDADES
							       // En mi caso T/T2
							       // esta en adp0->1

	if (fe_fd < 0 || dmx_fd < 0 || dvr_fd < 0) {
		fprintf(stderr, "Error abriendo el dispositivo! ¿Tienes permisos? (¿Grupo video o superusuario?)\n");
		return 1;
	}

	struct dtv_property props[] = {
		{ .cmd = DTV_DELIVERY_SYSTEM, .u.data = SYS_DVBT },
		{ .cmd = DTV_FREQUENCY,       .u.data = (uint32_t) frecuencia_hz },
		{ .cmd = DTV_BANDWIDTH_HZ,    .u.data = 8000000 },
		{ .cmd = DTV_TUNE }
	}; // Igual tienes que meter mas aqui. Pero a mi me vale con esto.
	// Para Hauppauge WinTV HVR 4000

	struct dtv_properties cmdseq = { .num = 4, .props = props };
	ioctl(fe_fd, FE_SET_PROPERTY, &cmdseq);

	struct dmx_pes_filter_params filter = {
		.pid = 0x2000,
		.input = DMX_IN_FRONTEND,
		.output = DMX_OUT_TS_TAP,
		.pes_type = DMX_PES_OTHER,
		.flags = DMX_IMMEDIATE_START
	};

	ioctl(dmx_fd, DMX_SET_PES_FILTER, &filter);

	char ruta[1024];

	snprintf(ruta, sizeof(ruta), "STREAMS/%s", argv[2]);

	FILE *fp = fopen(ruta, "wb");
	if (!fp) {
		perror("Error abriendo archivo, tienes permisos?");
		return 1;
	}

	uint8_t buf[188 * 1024];

	printf("Iniciando captura...\n");

	while (1) {
		ssize_t r = read(dvr_fd, buf, sizeof(buf));
		if (r <= 0)
			continue;

		fwrite(buf, 1, r, fp);

		for (int i = 0; i < r; i += TAMANO_PAQUETE_TS) {
			uint8_t *paquete = &buf[i];
			if (paquete[0] != 0x47)
				continue; // Paquete roto

			packets_checked++;
			uint16_t pid = obtener_pid(paquete);

			if (debug && packets_checked % 10000 == 0) {
				fprintf(stderr, "Paquetes analizados: %d, Coincidencias: %d\n", packets_checked, packets_matched);
			}

			if (!is_pid_set(pid)) {
				continue;
			}
			packets_matched++; // No, esto no esta duplicado, es largo, pero cuenta como doble

			int pusi = (paquete[1] >> 6) & 1; // payload_unit_start_indicator
			uint8_t afc = (paquete[3] >> 4) & 0x3;

			int offset = 4;
			if (afc == 0 || afc == 2) continue; // Sin payload
			if (afc == 3) offset += 1 + paquete[4]; // Saltar adaptation field
								// ¿¿¿Igual hay que hacer esto en algun momento???

			if (offset >= 188) continue; // Puede ser menos, pero no mas o igual (TS son 188 bytes cada paquete)

			const uint8_t *payload = paquete + offset;
			int payload_len = 188 - offset;

			// Nuevo PES? Preparar el antiguo
			if (pusi) {
				if (pes_actives[pid] && pes_lens[pid] > 0) {
					process_pes(pes_bufs[pid], pes_lens[pid], pid);
				}
				pes_lens[pid] = 0;
				pes_actives[pid] = 1;
			}

			if (!pes_actives[pid]) continue;

			// Overflow si el PES esta corrupto. Pero en el peor de los casos, tendra como medio megabyte POR HORA.
			// (Vamos, que se puede ignorar y dejar que Linux limpie cuando salgamos (NO USES ESTO DURANTE MESES SIN CERRAR))
			if (pes_lens[pid] + payload_len > PES_BUF_SIZE) {
				pes_actives[pid] = 0;
				pes_lens[pid] = 0;
				continue;
			}

			memcpy(pes_bufs[pid] + pes_lens[pid], payload, payload_len); // Lo mas rapido, pero inseguro
			pes_lens[pid] += payload_len;
		}
	}

	// Esto nunca pasa. Confiamos en que Linux limpie cuando acabemos... Nunca he dicho que se me da bien programar... Y menos en C, que estoy aprendiendo.
	close(fe_fd);
	close(dmx_fd);
	close(dvr_fd);
	fclose(fp);

	if (dec) vbi_decoder_delete(dec);

	for (int i=0; i<MAX_PID; i++){
		if (pes_bufs[i]) free(pes_bufs[i]);
	}

	return 0;
}

