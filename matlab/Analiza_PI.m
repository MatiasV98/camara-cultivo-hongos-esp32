%% analizar_PI.m
% Lee datos limpios de Prueba_PI_clean.csv y analiza desempeño del controlador PI

% Leer CSV
clear;clc;close all;
T = readtable('../datos/lazo_cerrado/pi_kc0.7_ti2400_sp22.csv');

t = T.Time_s;
y = T.Temperature_C;
sp = T.Setpoint_C;

% Graficar
figure;
plot(t/60, y, 'b', 'LineWidth',1.5); hold on;
plot(t/60, sp, 'r--', 'LineWidth',1.2);
xlabel('Tiempo (min)');
ylabel('Temperatura (°C)');
title('Respuesta del sistema con PI');
legend('Temperatura','Setpoint');
grid on;

%% Métricas de interés
tol = 0.1; % tolerancia en °C
% encontrar primer instante donde se mantiene dentro de banda
idx = find(abs(y - sp) <= tol, 1, 'first');
if ~isempty(idx)
    t_settle = t(idx);
else
    t_settle = NaN;
end

% Overshoot
overshoot = max(y) - sp(1);

% Error estacionario (últimos 50 puntos)
ss_error = mean(y(end-50:end) - sp(end));

%% Mostrar resultados
fprintf('Tiempo de establecimiento (±%.2f °C): %.1f s (%.1f min)\n', tol, t_settle, t_settle/60);
fprintf('Overshoot máximo: %.2f °C\n', overshoot);
fprintf('Error estacionario promedio: %.3f °C\n', ss_error);
