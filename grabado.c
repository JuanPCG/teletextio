#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <wchar.h> // Si lo hago bien no necesito hacer casting de tipos con wchar, pero no lo estoy haciendo bien

#include <sys/ioctl.h>

#include <linux/dvb/frontend.h>
#include <linux/dvb/dmx.h>
// Por que los includes estos quedan hasta bien y todo :)

#include <stdbool.h> // LITERALMENTE solo usado una vez para devolver un TRUE que podria ser un bit perfectamente.
#include <string.h>
#include <stdint.h>

#include <libzvbi.h> // EL QUE COMPILES!!! Usa lo de --enable-dvb en el configure antes del make!!
// POR DEFECTO EN DEBIAN VIENE SIN DVB!!!
// Este programa usa DVB (Obviamente)

#include <sys/stat.h>
#include <sys/types.h>


#define TAMANO_PAQUETE_TS 188 // No cambies esto
#define MAX_PID 8192 // Lo mas grande que he visto ha sido 4000 y algo, pero 8192 creo que es el tope. Los pids 'reales' empiezan como en el 20 o asi.
#define BITARRAY_SIZE ((MAX_PID + 7) / 8) // Son bits

// Esto es un poco sucio, por que es async! Pero en teoria casi que a lo justo
int pid_ahora = 0;
int pagina_busqueda = -1;


static vbi_decoder *dec = NULL; // libzvbi no te crea las declaraciones
static vbi_dvb_demux *demux = NULL;

static char *NOMBRE_CPT = NULL; // Para pasar el argv[1] del main al resto del programa.
static uint8_t pid_bitarray[BITARRAY_SIZE]; // Si esto no va a cambiar lo puedes poner tu mismo

char carpeta[1024]; // Una burrada, pero por si acaso


// Lo del debug ---
static int debug = 0;
static int packets_checked = 0; // Quitar esto si llego a eliminar lo del debug en este programa, que no creo
static int packets_matched = 0;
// ----------------

// Bit array functions
// Copiado de una página que no recuerdo
static inline void set_pid_bit(uint16_t pid) {
	if (pid < MAX_PID) {
		pid_bitarray[pid / 8] |= (1 << (pid % 8));
	}
}

static inline int is_pid_set(uint16_t pid) { // Ni puta idea pero funciona
	if (pid < MAX_PID) {
		return (pid_bitarray[pid / 8] & (1 << (pid % 8))) != 0;
	}
	return 0;
}

// Simple JSON parser for teletext PIDs
// Este codigo es horrible
// Lo saque de stack overflow
// Y añadi algo para avisar sobre errores
// Y lo adapte a el teletexto
// De hecho, la primera linea de este comentario decia
// Simple JSON parser for small ints
// PUEDE que se pudiese remplazar simplemente un argumento, o todos los argumentos tras argv[2]
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
	// Si hay mas de uno se puede buguear un poco, por que creo que no tengo bastantes ciclos para hacer TODO eso tan rapido.
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
	return ((paquete[1] & 0x1F) << 8) | paquete[2]; // Esto lo hacia tambien en el explorador de paquetes.
}

// Escapar caracteres JSON especiales
// Se que hago esto justo debajo, yo que se.
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
	// Me falta implementar esto en la parte del 'compilador'
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
		return; // Bastante obvio lo que hace
	}

	// Normal UTF-8 encoding
	// Esto lo habia sacado de FFMPEG, que (creo) que no se llega a usar nunca, por que no los he visto en uso. Los caracteres
	// estos raros digo. Pero es funcional, no se si en alguna version se habia implementado.
	// En el primer elif hay un espacio al final del fprint, NO LO BORRES!!!! Eso rompe los archivos, SI, ENSERIO.
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
		vbi_unref_page(&pg); // Des-cargamos la página
	}
}

int main(int argc, char *argv[]) {
	if (argc < 3) {
		printf("Uso: %s <entrada.ts> <archivo_pids.json> [debug]\n", argv[0]);
		printf("Ejemplo: %s captura_dia_32_diciembre.ts pids.json\n", argv[0]);
		printf("Debug: %s captura_dia_32_diciembre.ts pids.json debug\n", argv[0]);
		printf("Nota: El modo debug aqui es un poco inutil. Pero de tomas formas lo puedes usar para comprobar si esta cogiendo bien el PID\n");
		return 1;
	}

	NOMBRE_CPT = argv[1];
	char *base = strrchr(NOMBRE_CPT, '/'); // Si el nombre tiene un / nos quedamos solo con el final
	if (base)
		NOMBRE_CPT = base + 1; // Si no, se rompe al crear la carpeta por que intenta crear estilo 'carpeta_streams/stream1.ts' dentro de 'TELETEXTO', lo cual falla por que mkdir en C no crea padres.
	if (argc > 3 && strcmp(argv[3], "debug") == 0) {
		debug = 1;
		printf("Modo DEBUG activado\n");
	}

	memset(pid_bitarray, 0, BITARRAY_SIZE); // Creamos (Vacio) el array de pids (Hasta 8192, que es el tope (?) de DVB-T (Y para todos los DVB (T2, S, S2, C, etc)  (??))

	printf("Cargando PIDs de teletexto desde %s...\n", argv[2]);

	if (load_pids_from_json(argv[2]) == 0) {
		fprintf(stderr, "Error: No se pudieron cargar los PIDs\n");
		return 1;
	}


	char carp_base[10];
	snprintf(carp_base, sizeof(carp_base), "TELETEXTO");
	if (mkdir(carp_base, 0777) && errno != EEXIST) {
		printf("Error creando la carpeta contenedora!");
		return 1;
	}

	snprintf(carpeta, sizeof(carpeta), "TELETEXTO/%s", NOMBRE_CPT);
	if (mkdir(carpeta, 0777) && errno != EEXIST) {
		printf("Error creando carpeta!\n");
	}

	dec = vbi_decoder_new();
	if (!dec) {
		fprintf(stderr, "Error creando decoder VBI\n (Tienes libzvbi?)\n"); // O hacer un binario portable estatico, puto genio.
		return 1;
	}

	demux = vbi_dvb_pes_demux_new(pes_callback, dec);
	if (!demux) {
		fprintf(stderr, "Error creando demux DVB\n (Tienes libzvbi?)\n"); // El comentario de arriba :D
		vbi_decoder_delete(dec);
		return 1;
	}

	vbi_event_handler_add(dec, VBI_EVENT_TTX_PAGE, recibir_pagina, dec);

	FILE *fp = fopen(argv[1], "rb"); // Abrir como bytes.
	if (!fp) {
		perror("Error abriendo archivo TS"); // Yo que se, si no existe, me da pereza hacer un check de si algo existe
		return 1;
	}

	uint8_t buf[188 * 1024];

	printf("Procesando archivo TS...\n"); // Esto probablemente no se ve por que las páginas pasan como instantaneas

	while (1) {
		size_t r = fread(buf, 1, sizeof(buf), fp);

		if (r == 0) // O, cuando lleguemos al final de archivo midiendo el tamaño con sus bytes, por que esto queda como que feo cuando no es 'en directo', ya que sabemos el tamaño del archivo.
			break;

		for (int i = 0; i < r; i += TAMANO_PAQUETE_TS) { // No se donde poner esto, pero al principio pense que 'si cargo todo el archivo entero puedo hacer esto mucho mas rapido', esto obviamente es una idea mala, por que me salto el OOMK al segundo intento XD
			uint8_t *paquete = &buf[i];

			if (paquete[0] != 0x47)
				continue;

			packets_checked++;

			uint16_t pid = obtener_pid(paquete);

			if (debug && packets_checked % 10000 == 0) {
				fprintf(stderr, "Paquetes analizados: %d, Coincidencias: %d\n",
					packets_checked, packets_matched);
			}

			if (!is_pid_set(pid)) // Comprobar contra los pids
				continue;

			packets_matched++;

			if (debug) {
				fprintf(stderr, "PID coincidencia: 0x%04X (paquete %d)\n",
					pid, packets_checked);
			}

			uint8_t afc = (paquete[3] >> 4) & 0x3; // El offset del payload en un .ts, libzvbi tiene una funcion que hace practicamente esto. Pero como que era lo suficientemente simple para hacerlo aqui.

			int offset = 4;

			if (afc == 0 || afc == 2)
				continue;

			if (afc == 3) {
				uint8_t adaptation_length = paquete[4];
				offset += 1 + adaptation_length;
			}

			if (offset >= 188)
				continue;

			const uint8_t *payload = paquete + offset;

			unsigned int payload_len = 188 - offset;

			pid_ahora = pid;

			vbi_dvb_demux_feed(demux, payload, payload_len);
		}
	}

	printf("Finalizado.\n"); // Salir limpio

	fclose(fp); // En teoria el kernel libera el archivo al morir, pero por si acaso.

	if (demux)
		vbi_dvb_demux_delete(demux); // No hace falta, el demux y el decoder se borran al salir el padre.

	if (dec)
		vbi_decoder_delete(dec);

	return 0; // ME QUEDA IMPLEMENTAR ALGO PARA LOS ERRORES
}
