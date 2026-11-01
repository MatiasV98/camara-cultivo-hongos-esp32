%% limpiar_PI.m
% Lee el archivo crudo "Prueba_PI.txt" y genera un CSV limpio
% con columnas: Time_s, Temperature_C, Setpoint_C, Error_C, ControlSignal, Integral, RelayState, OnTime_s

fname = '../datos/crudos/Prueba_Final_PI_IMC.txt';
outname = 'Prueba_Final_PI_IMC_clean.csv';   % salida en la carpeta actual

% Leer todo el archivo como texto
txt = fileread(fname);

% Separar en líneas
lines = regexp(txt,'\r\n|\n|\r','split');

data = [];
for i = 1:length(lines)
    ln = strtrim(lines{i});
    if isempty(ln), continue; end
    
    % Saltar cabeceras o líneas que no empiezan con DATA o CONTROL
    if startsWith(ln,'DATA') || startsWith(ln,'CONTROL')
        % Quitar la primera palabra (DATA/CONTROL)
        parts = split(ln,',');
        if numel(parts) >= 9
            % Tomar columnas 2 a 9 (numéricas)
            nums = str2double(parts(2:9));
            if all(~isnan(nums))
                data(end+1,:) = nums; %#ok<AGROW>
            end
        end
    end
end

% Crear tabla
T = array2table(data, ...
    'VariableNames',{'Time_s','Temperature_C','Setpoint_C','Error_C','ControlSignal','Integral','RelayState','OnTime_s'});

% Guardar CSV limpio
writetable(T, outname);

fprintf('Archivo limpio guardado como %s con %d filas.\n', outname, height(T));
