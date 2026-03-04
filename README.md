# Teletextío
Un set de programas y scripts para hacer cosas con Streams MPEG, con un enfoque en cosas de teletexto

## capturar.c
	Un programa que captura en una frecuencia.
	Cambia las opciones (Adaptador) antes de compilar!
	Uso: ./capturar 581000000
		(Captura todo el mux que vea en 581Mhz)
	<!> ALERTA <!> Los streams tienden a producir muchos errores. Pronto subire una herramienta de correcion de errores.

## Pagina_A_Imagen.py
	Un script que saca una imagen de un archivo de teletexto usando FFMPEG (Asegurate de compilar con libzvbi)
	Uso: ./Pagina_A_Imagen.py todos.ts 800
		(Extrae del primer teletexto en 'todos.ts' la página 800)
			(Usa la herramienta ETT.py para sacar todos los teletextos como archivos individuales)

## ETT.py
	Un script que extrae todos los streams reconocidos como teletexto a archivos independientes usando FFMPEG.
	Uso: ./ETT.py 500_STREAMS.ts
		(Extrae cualquier stream de teletexto a 'teletextos-500_STREAMSts/')
