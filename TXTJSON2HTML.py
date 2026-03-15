#!/usr/bin/env python3
"""
Teletext JSON to HTML parser
Converts teletext JSON files to simple colored HTML
"""

import json, sys, os, pathlib, re, datetime

ruta_css = "../../../../estilos_txt_nuevo.css"
ruta_css_indice = "/css/pagina.css"
ruta_indice = "/teletexto/"
archivos = []
saltar1ra = True
arch_actual = 0
fallos_ascii = False
carpeta = ""
ruta_final = ""

bl_as = {
	#  Bloques 20-2F
	0x21: "🬀", 0x22: "🬁", 0x23: "🬂", 0x24: "🬃", 0x25: "🬄",
	0x26: "🬅", 0x27: "🬆", 0x28: "🬇", 0x29: "🬈", 0x2A: "🬉",
	0x2B: "🬊", 0x2C: "🬋", 0x2D: "🬌", 0x2E: "🬍", 0x2F: "🬎",
	# Bloques 30-3F
	0x30: "🬏", 0x31: "🬐", 0x32: "🬑", 0x33: "🬒", 0x34: "🬓",
	0x35: "▌", 0x36: "🬔", 0x37: "🬕", 0x38: "🬖", 0x39: "🬗",
	0x3A: "🬘", 0x3B: "🬙", 0x3C: "🬚", 0x3D: "🬛", 0x3E: "🬜", 0x3F: "🬝",
	# NOS SALTAMOS LOS 40-5F por que no son caracteres de bloques
	# Bloques 60-6F
	0x60: "🬞", 0x61: "🬟", 0x62: "🬠", 0x63: "🬡", 0x64: "🬢",
	0x65: "🬣", 0x66: "🬤", 0x67: "🬥", 0x68: "🬦", 0x69: "🬧",
	0x6A: "▐", 0x6B: "🬨", 0x6C: "🬩", 0x6D: "🬪", 0x6E: "🬫", 0x6F: "🬬",
	# Bloques 70-7F
	0x70: "🬭", 0x71: "🬮", 0x72: "🬯", 0x73: "🬰", 0x74: "🬱",
	0x75: "🬲", 0x76: "🬳", 0x77: "🬴", 0x78: "🬵", 0x79: "🬶",
	0x7A: "🬷", 0x7B: "🬸", 0x7C: "🬹", 0x7D: "🬺", 0x7E: "🬻", 0x7F: "█"
}



# Importa esta funcion para convertir rapido
def ascii_a_teletexto(numero):
	global fallos_ascii
	if numero > 60000:
		cod_teletexto = numero & 0x7F
		if cod_teletexto < 0x20:
			return " " # Es un caracter de control (Tengo que implementar esto, igual usar lo que ya tengo en el json, o marcarlo desde aqui)
		elif cod_teletexto == 32:
			return " " # Espacio
		elif cod_teletexto in bl_as:
			return bl_as[cod_teletexto]
		else:
			fallos_ascii = True # Pone un error en la página
			return f"(0x{cod_teletexto})" # El codigo, para identificarlo
	else:
		return "#"

# Los colores que sacara de LibZVBI, puedes cambiarlos para HTML
TTX_COLORS = {
	0: '#000000',  # Negro
	1: '#FF0000',  # Rojo
	2: '#00FF00',  # Verde
	3: '#FFFF00',  # Amarillo
	4: '#0000FF',  # Azul
	5: '#FF00FF',  # Magneta
	6: '#00FFFF',  # Azul cian
	7: '#FFFFFF',  # Blanco
}

def color_desde_txt(color_code):
	return TTX_COLORS.get(color_code & 0x7, '#FFFFFF')

def info_usable_caracter(datos):
	return {
		'fg': color_desde_txt(datos.get('foreground', 7)),	# Por defecto blanco para frente
		'bg': color_desde_txt(datos.get('background', 0)),	#   ~    ~    negro   ~   fondo
		'bold': datos.get('bold', 0),				# Si es 'negrita' la ponemos, si no, le decimos que no
		'italic': datos.get('italic', 0),			# Italicas
		'underline': datos.get('underline', 0),			# Auto-explicativo
		'flash': datos.get('flash', 0),				# Algunas páginas 'parpadean' con texto (Antena de Radio de RTVE, por ejemplo)
		'conceal': datos.get('conceal', 0),			# Esto no se usa en España ni en Europa en general, PERO por si acaso.
									# /* Hack de transparencia de -VLC- FFMPEG */
	}



mfm_tabla = {
    "fg" : "$[fg.color=%ATTR %CHAR]",
    "bg" : "$[fg.color=%ATTR %CHAR]",
    "bold" : "**%CHAR**",
    "italics": "*%CHAR*",
    "italics": "*%CHAR*",

}



# def styles_equal(style1, style2):
#	return style1 == style2

# ^ Este codigo solo era un IF que no sabia como poner abajo

def create_html_from_json(json_file): # La mayoria de este codigo esta copiado de otras cosas. Pero esta adaptado para lo que necesito.
	global fallos_ascii
	with open(json_file, 'r', encoding='utf-8') as f:
		data = json.load(f)

		html = f"""<!DOCTYPE html>
		<html lang="es">
		<head>
		<meta charset="UTF-8">
		<link rel="stylesheet" href="{ruta_css}">
		<title>{os.path.basename(json_file).replace('.json','')}</title>
		</head>
		<body>
		"""
		if 'contenido_detallado' in data:
			contenido = data['contenido_detallado']
			lin_actual = 0
			lineas_vistas = {}
			for line in contenido:
				if lin_actual == 0 and saltar1ra:
					print("Saltando primera linea...")
					lin_actual+=1
					lineas_vistas[0] = "{RELOJ}"
					continue
				div_esta = ""
				html+='<div class="line">'				# Lo de no repetir spans por cada cosa
				i = 0
				while i < len(line):
					char_data = line[i]
					current_style = info_usable_caracter(char_data)
					# Empezamos el segmento de texto
					text_segment = ""
					j = i
					while j < len(line) and (info_usable_caracter(line[j]) == current_style):
						char_info = line[j]
						if char_info['unicode'] > 60000: # Hack codigos >60000 de libzvbi
							text_segment += ascii_a_teletexto(char_info['unicode'])
						elif char_info['unicode'] > 0:
							text_segment += chr(char_info['unicode'])
						else:
							text_segment += ' '
						j += 1
					# Ponemos los HTML-codes en vez de caracteres que pueden romper HTML
					text_segment = text_segment.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')

					# Los <span> llevan clases, aqui las ponemos.
					classes = []
					if current_style['bold']:
						classes.append('bold')
					if current_style['italic']:
						classes.append('italic')
					if current_style['underline']:
						classes.append('underline')
					if current_style['flash']:
						classes.append('flash')
					if current_style['conceal']:
						classes.append('conceal')

					class_str = ' '.join(classes) # Tambien se puede hacer for clase in classes: class_str += clase
					class_attr = f' class="{class_str}"' if class_str else '' # Si no tenemos clases, no ponemos nada, por que va justo despues del <span> sin espacio.
					linea_F = f'<span{class_attr} style="color: {current_style["fg"]}; background-color: {current_style["bg"]};">{text_segment}</span>' # La linea, ya construida
					lineas_vistas[lin_actual] = f"{linea_F}"

#					div_esta = '<div class="line">'
#					html+=div_esta
					html+=linea_F
#					if lineas_vistas[lin_actual-1] == linea_F:
#						nuevo_html = html[:-len(linea_F)]
#						html=nuevo_html
#						print(f"LINEA REPETIDA EN POSICION {lin_actual}, contenido: {linea_F}\nEs igual a {lineas_vistas[lin_actual-1]}")
#						div_esta = '<div class="line doble">'
#						html += div_esta
#						html += linea_F
#					else:
#						print(f"Linea {lin_actual} no se repite...")
#						div_esta = '<div class="line">'
#						html += div_esta
#						html += linea_F
					lin_actual+=1
					i = j # Los estilos (Poner el 'nuevo' en el 'anterior')
				html += '</div>\n' # Cerramos el div de esta linea
		else: # SI NO TENEMOS CONTENIDO EN EL JSON
			if 'contenido_texto' in data: # 'Convertimos' el que renderiza capturar.c
				for line in data['contenido_texto']: # Feo, intenta que no pase.
					html += f'<div class="line">{line.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")}</div>\n'

		# Parte 'footer' (Enlaces)
		html+="<hr>"
		try: # Si tenemos una página antes
			anterior = os.path.basename(archivos[arch_actual-1]).replace('.json','.html')
			html += f"""<p><a href="{anterior}">Página anterior</a> | """
		except Exception as E:
			print("Falta la anterior!")
			print(E)
		try: # Si tenemos una página siguiente
			siguiente = os.path.basename(archivos[arch_actual+1]).replace('.json','.html')
			html += f"""<a href="{siguiente}">Página siguiente</a></p>"""
		except Exception as E:
			print("Falta la siguiente!")
			print(E)

		html += f"""<p><a href="{ruta_indice}">[Indice principal]</a> | """
		html += f"""<a href="index.html">[Indice]</a></p>"""
		if fallos_ascii: # Si 'ascii_a_teletexto' no encuentra eso, ponemos un pequeño aviso con un enlace a el GitHub, por si alguien sabe que hacer para arreglarlo.
			html+="""<p>Alerta: Se han generado problemas en esta página, mira los bloques que fallan (0x00)</p>"""
			html+="""<p>Si puedes, por favor, <a href="https://github.com/JuanPCG/teletextio/tree/master">contribuye</a> para arreglar esto</p>"""
			fallos_ascii = False
		html +="""
			</body>
		</html>
		"""
		return html # Igual que py_final.py, no escribimos aqui, escribimos fuwera.

def encolar(ruta): # Llamamos aqui con los archivos, individuales
	print(f"Empezando a procesar... {ruta}")
	try:
		html_content = create_html_from_json(ruta)
		with open(f"{ruta_final}/{os.path.basename(ruta).replace('.json','.html')}", 'w', encoding='utf-8') as f:
			f.write(html_content)
		print(f"✓ HTML generado: {ruta}")
	except json.JSONDecodeError as e:
		print(f"Error parseando JSON: {e}")
		sys.exit(1)
	except Exception as e:
		print(f"Error: {e}")
		sys.exit(1)

def sort_key(file_path): # Esta funcion hace 'sort', a los archivos, para que empiece por 100-00, 100-01, 101-00... ETC...
	parts = re.split(r'[-_]', file_path.stem)
	return (int(parts[0]), int(parts[1]))

def salir_y_generar_HTML(): # Generamos el index.html>
	html_index = ""
	html_index += f"<html>"
	html_index += f"	<head>"
	html_index += f"""		<link rel="stylesheet" href="{ruta_css_indice}">"""
	html_index += f"""		<title>Dump</title>"""
	html_index += f"	</head>"
	html_index += f"	<body>"
	html_index += f"		<p>Páginas vistas: {len(archivos)}</p>"
	html_index += f"		<a href='{ruta_indice}'>Volver al indice</a>"
	html_index += f"		<ul>"
	for i in archivos:
		html_index += f"""			<li><a href="{os.path.basename(str(i).replace(".json",".html"))}">Página {os.path.basename(str(i).replace(".json",""))}</li>"""
	html_index += f"		</ul>"
	html_index += f"	</body>"
	html_index += f"</html>"
	with open(f"{ruta_final}/index.html", 'w', encoding='utf-8') as f:
		f.write(html_index)
	print(f"{len(archivos)} páginas procesadas") # Esto no es parte del HTML, simplemente decimos adios y ya




if len(sys.argv) < 3:
	print("Uso: python3 t.py <archivo o directorio> <canal>")
	sys.exit(1)
# ------------------------------------[ 1:1 copiado de mi otro script que estaba usando hasta ahora ]---------------------------------------
canal = sys.argv[2]
fecha = datetime.datetime.now()
año, mes, dia = fecha.year, fecha.month, fecha.day
os.makedirs(f"{año}/{mes}/{dia}/{canal}", exist_ok=True)
ruta_final=f"{año}/{mes}/{dia}/{canal}"
json_file = sys.argv[1]
if not os.path.exists(json_file):
	print(f"Error: El archivo o carpeta'{json_file}' no existe")
	sys.exit(1)

if os.path.isdir(json_file):
	car = pathlib.Path(json_file)
	archivos = list(car.glob('*.json'))
	archivos.sort(key=sort_key)
	for i in archivos:
		encolar(f"{i}")
		arch_actual += 1
else:
	print("Trabajando en archivos individuales!")
	encolar(json_file)
salir_y_generar_HTML()



