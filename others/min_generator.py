
# Autor: João Marcos Lana Gomes
# Dezembro, 2018
# 
#   - Rotina que converte arquivos do WebPages em versões resumidas no diretório WebPages/min;
#   - Gera versões resumidas no formato .min.css e .min.js;
#   - Por fim gera os arquivos .ino.html, no qual substitui link's do tipo css (<link type="stylesheet" [...]>) e js (<style src=[...]></style>) no conteudo dos respectivosarquivos.


import os
import re


dir = os.path.join(os.path.dirname(__file__), os.path.pardir, 'WebPages')
new_dir = os.path.join(dir, 'min')


# Work with css and js files
def minify(filename, dir_ori, dir_dest=None, new_name=None):
    if dir_dest is None:
        dir_dest = dir_ori
    
    if new_name is None:
        new_name = filename.replace('.', '.min.')
    
    dir_ori = os.path.join(dir_ori, filename)
    dir_dest = os.path.join(dir_dest, new_name)

    text = ''

    with open(dir_ori) as file:
        for line in file:
            text += line.replace('\n', '').strip()

    with open(dir_dest, 'w') as file:
        file.write(text)

# Search files with their ext in the dir, then run func with args
def searchByExt(dir, ext, func, args=None):
    for file in os.listdir(dir):
        if os.path.isfile(os.path.join(dir, file)):
            file_name, file_extension = file.split('.')
            if (type(ext) == str and file_extension == ext) or (type(ext) == list and file_extension in ext):
                if args is not None:
                    _args = args.copy()
                    if '\\file' in args:
                        _args[args.index('\\file')] = file
                    func(*_args)
                else: 
                    func()

# Convert Html to one line arduino html
def toArduinoHtml(filename, dir_ori, dir_min=None, new_name=None):
    if dir_min is None:
        dir_min = dir_ori
    
    if new_name is None:
        new_name = filename.replace('.', '.ino.')
    
    dir_ori = os.path.join(dir_ori, filename)

    # Replace css and js link to their matter
    def tag_replace(matchObj):
        nonlocal dir_min

        file_name = matchObj.group(1).replace('.', '.min.')
        ext = matchObj.group(1).split('.')[1]
        
        with open(os.path.join(dir_min, file_name)) as f:
            if ext == 'css':
                return '<style>' + f.read() + '</style>'
            elif ext == 'js':
                return '<script>' + f.read() + '</script>'

    html = ''

    with open(dir_ori) as f:
        for line in f:
            line = re.sub(r'<link rel="stylesheet" href="(.*\.css)">', tag_replace, line)
            line = re.sub(r'<script src="(.*\.js)"></script>', tag_replace, line)
            line = line.replace('\n', '').replace('\"', '\\"').strip()
            html += line

    with open(os.path.join(dir_min, new_name), 'w') as f:
        f.write(html)

# Main()
if __name__ == '__main__':
    global dir, new_dir

    # Before, generate min of all css and js files
    searchByExt(dir, ['js', 'css'], minify, ['\\file', dir, new_dir])
    
    # After, create min of html's replacing css and js
    searchByExt(dir, 'html', toArduinoHtml, ['\\file', dir, new_dir])

    print('\033[1;92m' + '\n\n--- Terminado sem nenhum erro! ---\n\n' + '\033[0;37m')
