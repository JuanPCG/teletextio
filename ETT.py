#!/usr/bin/env python
import subprocess, json, sys, os
ctelex = ["dvb_teletext"] # LA LISTA DE TODOS LOS 'CODECS' ACEPTABLES COMO TELETEXTO!!!
tambien_extraer_subtitulos = False
cmd = [
	'ffprobe',
	'-v', 'error',
	'-show_streams',
	'-print_format', 'json',
]
if len(sys.argv) != 2:
	print("Uso: (script.py) archivo-stream")
	exit()
else:
	ruta = sys.argv[1]
	cmd.append(ruta)
	try:
		os.mkdir(f"teletextos_{ruta.replace('/','').replace('.','')}")
	except Exception as e:
		print(f"Error al crear la carpeta contenedora\n{e}")
		exit()
try:
	fin = subprocess.run(cmd, capture_output=True, text=True, check=True)
	d = json.loads(fin.stdout)
	lista = []
	for stream in d.get('streams', []):
		if stream.get('codec_name') in ctelex:
			lista.append(stream.get('index'))
except subprocess.CalledProcessError as e:
	f"No se puede ejecutar ffprobe: {e}"
except Exception as e:
	f"Error: {e}"

for i in lista:
	print(f"Extrayendo teletexto con id {i}...")
	cmd_extraer_id = ["ffmpeg", "-loglevel", "error","-hide_banner", "-y", "-i", ruta, "-map", f"0:{i}", "-c", "copy", f"teletextos_{ruta.replace('/','').replace('.','')}/txt_pid{i}.ts"]
	try:
		subprocess.run(cmd_extraer_id)
		print("Extraccion satisfactoria!")
	except Exception as e:
		print(f"Error en {i}\nError: {e}")
