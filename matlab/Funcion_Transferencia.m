clc;clear;close all;
% ---- Parámetros (cambiá por los tuyos si querés) ----
Kp  = 24.50;      % ganancia (Datos_6h)
L   = 133;      % dead-time [s]
tau = 2928.6;     % constante de tiempo [s]

% ---- Crear TF nominal sin retardo ----
s = tf('s');
G_nodelay = Kp/(tau*s + 1);

% ---- Aproximación Pade del retardo (orden 1) ----
[n_d, d_d] = pade(L,1);    % devuelve numerador y denominador del Pade
Delay_approx = tf(n_d, d_d);

% ---- Planta aproximada (racional) ----
G_approx = Delay_approx * G_nodelay;   % TF aproximada: K * pade(L)/ (tau s + 1)

% ---- Mostrar TF aproximada ----
disp('Planta aproximada (racional con Pade):');
G_approx

% ---- Root locus (LGR) ----
figure;
rlocus(G_approx);
title('Root Locus de la planta aproximada (Pade delay)');

% ---- Bode / Margen ----
figure;
margin(G_approx);
[GM,PM,Wcg,Wcp] = margin(G_approx);  % GM en factor absoluto, PM en grados
% mostrar valores
fprintf('Gain margin (GM) = %g  (factor), Phase margin (PM) = %.2f deg\n', GM, PM);
fprintf('Wcg (freq where phase = -180) = %.4f rad/s, Wcp (freq where gain=0dB) = %.4f rad/s\n', Wcg, Wcp);

% ---- Ganancia crítica (K_crítico) para lazo unidad ----
% margin devuelve GM (factor) tal que al multiplicar lazo por GM se llega a inestabilidad
% Si modelo G_approx representa tu planta con ganancia Kp (es decir lazo actual = G_approx),
% entonces K_crit = GM (si lazo actual tiene ganancia 1). 
% Para mayor claridad, si quieres encontrar Kcrit tal que Kcrit*G_approx en lazo unitario se vuelva marginal:
Kcrit = GM;
if isinf(Kcrit)
    fprintf('Gain margin infinite -> sistema no muestra cruce de -1 en bode (no hay Kcrit finito).\n');
else
    fprintf('Ganancia crítica Kcrit ≈ %.4g (factor multiplicativo sobre lazo abierto actual).\n', Kcrit);
end

% ---- Ejemplo: simular lazo cerrado con un controlador proporcional Kc
Kc_list = [0.1, 0.5, 1, 2, Kcrit];
figure; hold on;
for Kc = Kc_list
    Tcl = feedback(Kc * G_approx, 1);   % lazo unidad
    step(Tcl, 0:100000); % tiempo amplio (señal térmica lenta)
    % podrías reducir el tiempo a e.g. 0:6000 s (100 min) si quieres
end
legend('Kc=0.1','Kc=0.5','Kc=1','Kc=2','Kc=Kcrit (aprox.)');
title('Respuesta en lazo cerrado con distintos Kp (proporcionales)');
xlabel('Tiempo (s)');

% ---- Observación del orden real del modelo ----
% Por la forma y la identificación, el modelo es esencialmente 1er orden con retardo.
% La Pade aumenta el orden racional al (1 + ordenPade*2) aproximadamente.
