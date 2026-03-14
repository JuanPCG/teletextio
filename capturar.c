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

// Esto es un poco sucio, por que es async! Pero en teoria casi que a lo justo
int pid_ahora = 0;
int pagina_busqueda = -1;


static vbi_decoder *dec = NULL;
static vbi_dvb_demux *demux = NULL;

static char *NOMBRE_CPT = NULL;
static uint8_t pid_bitarray[BITARRAY_SIZE];

char carpeta[1024];

static int debug = 0;
static int packets_checked = 0;
static int packets_matched = 0;


// Bit array functions
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

// Simple JSON parser for teletext PIDs
static int load_pids_from_json(const char *json_file) {
	FILE *f = fopen(json_file, "r");
	if (!f) {
		fprintf(stderr, "Error: No se puede abrir %s\n", json_file); // 🐛🐛
		return 0;
	}

	char buffer[65536];
	size_t bytes_read = fread(buffer, 1, sizeof(buffer) - 1, f); // 🐛🐛
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

	const char *bracket = strchr(pids_start, '['); // 🐛🐛🐛
	if (!bracket) {
		fprintf(stderr, "Error: Formato JSON inválido\n");
		return 0;
	}

	int pid_count = 0;
	const char *p = bracket + 1; // 🐛🐛

	while (*p && *p != ']') { // 🐛🐛
		while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ',')) {
			p++;
		}

		if (*p >= '0' && *p <= '9') {
			long pid = strtol(p, (char **)&p, 10);
			if (pid >= 0 && pid < MAX_PID) {
				set_pid_bit((uint16_t)pid);
				pid_count++;
				printf("  PID cargado: %ld (0x%04lX)\n", pid, pid);
			}
		} else if (*p == ']') {
			break;
		} else {
			p++;
		}
	}

	printf("Total de %d PIDs de teletexto cargados\n", pid_count);
	return pid_count; // 🐛🐛
}

// PES a Sliced para zvbi
static vbi_bool pes_callback(vbi_dvb_demux *dx, void *user_data, const vbi_sliced *sliced, unsigned int lines, int64_t pts) {
	vbi_decoder *decoder = (vbi_decoder *) user_data;
	double timestamp = pts / 90000.0;
	vbi_decode(decoder, sliced, lines, timestamp);
	return TRUE;
}

// Extraemos el PID
static uint16_t obtener_pid(const uint8_t *paquete) {
	return ((paquete[1] & 0x1F) << 8) | paquete[2];
}

// Escapar caracteres JSON especiales
static void escape_json_string(FILE *f, const char *str) {
	if (!str) return;
	while (*str) {
		switch (*str) {
			case '"':  fprintf(f, "\\\""); break;
			case '\\': fprintf(f, "\\\\"); break;
			case '\b': fprintf(f, "\\b");  break;
			case '\f': fprintf(f, "\\f");  break;
			case '\n': fprintf(f, "\\n");  break;
			case '\r': fprintf(f, "\\r");  break;
			case '\t': fprintf(f, "\\t");  break;
			default:
				if ((unsigned char)*str < 0x20) {
					fprintf(f, "\\u%04x", (unsigned char)*str);
				} else {
					fputc(*str, f);
				}
		}
		str++;
	}
}

// Convertir unicode a UTF-8 y escribir en el archivo
static void write_unicode_char(FILE *f, uint32_t unicode) {
	if (unicode == 0) {
		fprintf(f, " ");
		return;
	}
	
	// Escape special JSON characters
	switch (unicode) {
		case '"':  fprintf(f, "\\\""); return;
		case '\\': fprintf(f, "\\\\"); return;
		case '\b': fprintf(f, "\\b");  return;
		case '\f': fprintf(f, "\\f");  return;
		case '\n': fprintf(f, "\\n");  return;
		case '\r': fprintf(f, "\\r");  return;
		case '\t': fprintf(f, "\\t");  return;
	}
	
	// For control characters, use unicode escape
	if ((unsigned char)unicode < 0x20) {
		fprintf(f, "\\u%04x", unicode);
		return;
	}
	
	// Normal UTF-8 encoding
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
}

// Cuando recibe una página nueva
static void recibir_pagina(vbi_event *ev, void *user_data) {
	char subcarp[2048]; // Mucho, pero para probar
	snprintf(subcarp, sizeof(subcarp), "TELETEXTO/%s/PID%d", NOMBRE_CPT, pid_ahora);
	mkdir(subcarp, 0777); // Mirame, soy un programador!
	if (ev->type != VBI_EVENT_TTX_PAGE) return;
	vbi_decoder *decoder = (vbi_decoder *) user_data;
	vbi_page pg;
	if (pagina_busqueda >= 0 && pg.pgno != pagina_busqueda) {
		return;
	}
	if (vbi_fetch_vt_page(decoder, &pg, ev->ev.ttx_page.pgno, ev->ev.ttx_page.subno, VBI_WST_LEVEL_3p5, 25, 0)) {
		printf("Encontrado bloque teletexto: %03X-%02X del PID %d\n", pg.pgno, pg.subno, pid_ahora); //
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
	}
}


int main(int argc, char *argv[]) {
	if (argc < 4) {
		printf("Uso: %s <frecuencia_en_Hz> <archivo> <archivo_pids.json> [debug]\n", argv[0]);
		printf("Ejemplo: %s 674000000 captura_dia_32_diciembre.ts pids.json\n", argv[0]);
		printf("Debug: %s 674000000 captura_dia_32_diciembre.ts pids.json debug\n", argv[0]);
		return 1;
	}

	NOMBRE_CPT = argv[2];
	if (argc > 4 && strcmp(argv[4], "debug") == 0) {
		debug = 1;
		printf("Modo DEBUG activado\n");
	}

	long frecuencia_hz = atol(argv[1]);

	printf("Cambiando a frecuencia: %ld Hz...\n", frecuencia_hz);

	memset(pid_bitarray, 0, BITARRAY_SIZE);

	printf("Cargando PIDs de teletexto desde %s...\n", argv[3]);
	if (load_pids_from_json(argv[3]) == 0) {
		fprintf(stderr, "Error: No se pudieron cargar los PIDs\n");
		return 1;
	}
	char carp_base[10];
	snprintf(carp_base, sizeof(carp_base), "TELETEXTOS");
	if (mkdir(carp_base, 0777) && errno != EEXIST) { // Si ya existe no es un 'error'
		printf("Error creando la carpeta contenedora!");
		return 1;
	}

	char carp_restos_streams[7];
	snprintf(carp_restos_streams, sizeof(carp_base), "STREAMS");
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

	demux = vbi_dvb_pes_demux_new(pes_callback, dec);
	if (!demux) {
		fprintf(stderr, "Error creando demux DVB\n (Tienes libzvbi?)\n");
		vbi_decoder_delete(dec);
		return 1;
	}

	vbi_event_handler_add(dec, VBI_EVENT_TTX_PAGE, recibir_pagina, dec);

	int fe_fd  = open("/dev/dvb/adapter0/frontend1", O_RDWR);
	int dmx_fd = open("/dev/dvb/adapter0/demux1", O_RDWR);
	int dvr_fd = open("/dev/dvb/adapter0/dvr1", O_RDONLY);

	if (fe_fd < 0 || dmx_fd < 0 || dvr_fd < 0) {
		fprintf(stderr, "Error abriendo el dispositivo! ¿Tienes permisos? (¿Grupo video o superusuario?)\n");
		return 1;
	}

	struct dtv_property props[] = {
		{ .cmd = DTV_DELIVERY_SYSTEM, .u.data = SYS_DVBT },
		{ .cmd = DTV_FREQUENCY,       .u.data = (uint32_t) frecuencia_hz },
		{ .cmd = DTV_BANDWIDTH_HZ,    .u.data = 8000000 },
		{ .cmd = DTV_TUNE }
	};

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

	char ruta[1024]; // Mucho, pero por si acaso

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
			packets_matched++;
			if (debug) {
				fprintf(stderr, "PID coincidencia: 0x%04X (paquete %d)\n", pid, packets_checked);
			}
			
			uint8_t afc = (paquete[3] >> 4) & 0x3;
			int offset = 4;
			if (afc == 0 || afc == 2) {
				if (debug) fprintf(stderr, "  AFC = %d, sin payload\n", afc);
				continue;
			}
			if (afc == 3) {
				uint8_t adaptation_length = paquete[4];
				offset += 1 + adaptation_length;
			}
			if (offset >= 188) {
				if (debug) fprintf(stderr, "  offset >= 188\n");
				continue;
			}
			
			const uint8_t *payload = paquete + offset;
			unsigned int payload_len = 188 - offset;
			if (debug) fprintf(stderr, "  Enviando payload de %d bytes al demux\n", payload_len);
			pid_ahora = pid;
			vbi_dvb_demux_feed(demux, payload, payload_len);

		}
	}
	
	// Cleanup
	close(fe_fd);
	close(dmx_fd);
	close(dvr_fd);
	fclose(fp);
	if (demux) vbi_dvb_demux_delete(demux);
	if (dec) vbi_decoder_delete(dec);
	
	return 0;
}
