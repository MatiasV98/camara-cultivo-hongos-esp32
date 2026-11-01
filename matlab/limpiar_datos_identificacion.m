%% limpiar_datos_identificacion.m
% Limpia un archivo tipo "Prueba_6h.txt" y genera CSV con columnas:
% Tiempo_s, Temperatura_C, Fase
clear; clc;

fname = '../datos/crudos/Prueba_6h.txt';           % archivo crudo (cambiar si hace falta)
outname = 'Prueba_6h_clean.csv';     % archivo limpio (salida)

% Leer archivo como texto
txt = fileread(fname);
lines = regexp(txt,'\r\n|\n|\r','split');

% Inicializar contenedores
data = zeros(0,2);
fases = strings(0,1);

% Patrón tolerante: tiempo,temperatura,fase (fase C o E, posible espacio)
pat = '^\s*([0-9]+(?:\.[0-9]+)?)\s*,\s*([0-9]+(?:\.[0-9]+)?)\s*,\s*([CE])\s*$';

for i = 1:numel(lines)
    ln = strtrim(lines{i});
    if isempty(ln)
        continue;
    end
    tok = regexp(ln, pat, 'tokens');
    if ~isempty(tok)
        t = str2double(tok{1}{1});
        temp = str2double(tok{1}{2});
        fase = string(tok{1}{3});
        % Guardar
        data(end+1, :) = [t, temp];       %#ok<AGROW>
        fases(end+1, 1) = fase;           %#ok<AGROW>
    end
end

% Verificar extracción
if isempty(data)
    error('No se extrajeron líneas válidas del archivo. Revisa el formato.');
end

% Crear tabla y guardar CSV
T = array2table(data, 'VariableNames', {'Tiempo_s','Temperatura_C'});
T.Fase = fases;
writetable(T, outname);
fprintf('Archivo limpio guardado como %s con %d muestras.\n', outname, height(T));

% Opcional: mostrar primeras filas
disp(T(1:min(8,height(T)),:));
