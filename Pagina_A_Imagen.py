#!/bin/env python3

import os, sys, subprocess, mimetypes
header=False

lista_hex_validos = [
	b'\x47',			# Transport Stream
	b'\x1A\x45\xDF\xA3',		# Matroska (.mkv)
	b'RIFF',			# AVI (Audio Video noseque)
	b'\x00\x00\x00\x18ftyp',	# MP4 (GENERAL <!>)
]

if len(sys.argv) != 3:
	print(f"Uso: {sys.argv[0]} [archivo-entrada] [n-pag]") # El script en si (./img.py) ya es un argv
	exit()

try:
	int(sys.argv[2])
except Exception as error:
	print(f"""No se puede procesar {sys.argv[2]}:\n{error}""")
	exit()

if not os.path.exists(sys.argv[1]):
	print(f"Archivo no encontrado: {sys.argv[1]}")
	exit()

with open(sys.argv[1], 'rb') as f:
	Cabecera = f.read(16) # 16 deberia ser suficiente para la cabecera de un archivo
	for tipo_mime in lista_hex_validos:
		if Cabecera.startswith(tipo_mime):
			valido=True

if not valido:
	print("La cabecera no coincide con un formato de video conocido...\nProcediendo de todos modos\nSi hay errores, ejecuta FFPROBE sobre el archivo")
else:
	print("Cabecera validada")

dir = f"{os.path.basename(os.sys.argv[1])}-pagina-{sys.argv[2]}"

try:
	os.mkdir(dir)
except FileExistsError as error:
	print("El directorio ya existe... Procediendo de todos modos")
except Exception as error:
	print(f"Error desconocido: {error}")
	exit()

cmd = ["ffmpeg", "-hide_banner", "-txt_format", "bitmap", "-txt_page", sys.argv[2], "-i", sys.argv[1], "-filter_complex", "[0:s:0]scale=720:576:flags=neighbor,mpdecimate[v]", "-map", "[v]", "-vsync", "vfr", f"{dir}/%03d.png"]
try:
	ff = subprocess.Popen(cmd, stderr=subprocess.PIPE, universal_newlines=True, encoding='utf-8')
except:
	exit()
for linea in ff.stderr:
	if 'frame=' in linea:
		print(linea.strip(), end="\r")
	sys.stdout.flush()
print("\n")
print("Ala, ahi tienes")
