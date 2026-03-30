#!/usr/bin/env python3

def guardar_archivo(ruta, texto):
	"""Guarda un texto en un archivo"""
	try:
		archivo = open(ruta, 'a')
	except Exception as e:
		print(f"No se ha podido abrir el archivo, el error era:\n{e}")
		return False
	archivo.write(texto) # Aqui aun puede fallar.
	return True

def cli(): # Lo que vendria a ser el main
	import sys, os, TXTJSON2HTML
	if len(sys.argv) <= 1:
		print(f"Uso: {sys.argv[0]} (archivo-entrada)")
		return False
	else:
		print(f"Procesando el archivo... {sys.argv[1]}")
	try:
		archivo_entrada = open(sys.argv[1],"r")
	except Exception as e:
		print(f"No se ha podido abrir el archivo, el error era:\n{e}")
		return False
	texto_inicial = archivo_entrada.read()
	for letra in texto_inicial:
		if ord(letra) >= 60000: # Cods teletexto
			letra_fin = TXTJSON2HTML.ascii_a_teletexto(ord(letra)) # En realidad podemos probar contra todos, la funcion en TXTJSON2HTML devuelve el caracter si no es un caracter de teletexto
			guardar_archivo(f'{sys.argv[1].replace("/","-")}-PROCESADO',letra_fin) # La ruta un poco hack
			print(f"Convertido codigo {ord(letra)} a {letra_fin}") # O a lo mejor un contador?
		else:
			guardar_archivo(f'{sys.argv[1].replace("/","-")}-PROCESADO',letra) # Lo guardamos tal cual

if __name__ == "__main__":
	cli()
