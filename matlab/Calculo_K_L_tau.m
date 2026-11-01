% Calculo_K_L_tau_corregido.m
clear; clc;

% --- leer datos (asegurate del nombre y extensión) ---
data = readtable('../datos/identificacion/prueba1_100min.csv'); % ensayo a analizar
%data = readtable('../datos/identificacion/prueba3_240min.csv');
t = data.Tiempo_s;
y = data.Temperatura_C;

% eliminar NaN si existieran
ok = ~isnan(t) & ~isnan(y);
t = t(ok);
y = y(ok);

% valores inicial/final
T0 = y(1);
Tinf = mean(y(end-min(60,end):end)); % promedio últimos hasta 60 muestras
Delta = Tinf - T0;

if Delta <= 0
    warning('Delta <= 0: verifique que el ensayo sea de calentamiento y que los datos estén bien.');
end

% --- modelo anónimo (definir antes del costo) ---
% p = [L, tau]
y_model = @(p,tt) ( tt < p(1) ).* T0 + ( tt >= p(1) ).*( Tinf - Delta.*exp(-(tt - p(1))./p(2)) );

% --- función de costo (suma de cuadrados) ---
cost = @(p) sum( ( y - y_model(p, t) ).^2 );

% --- estimación inicial razonable ---
% tentativa para L: buscar primer cambio pequeño relativo; si no, L0=0
dys = smooth(diff(y),5);
idx_change = find(abs(dys) > 0.005*abs(Delta) , 1); % umbral relativo
if isempty(idx_change)
    L0 = 0;
else
    L0 = t(max(1,idx_change)); 
end
% tau0: tiempo hasta 63% del cambio (si existe)
idx63 = find(y >= T0 + 0.632*Delta, 1);
if ~isempty(idx63)
    tau0 = max(1, t(idx63) - L0);
else
    tau0 = max(10, (t(end)-L0)/4);
end
p0 = [L0, tau0];

% --- usar fminsearch con penalización pero con protección ---
pen = @(p) cost(max([eps, p(1); eps, p(2)]')); % fuerza p>0 (vectorize y protege 0)
options = optimset('Display','iter','MaxIter',4000,'TolX',1e-6,'TolFun',1e-6);

[p_est, fval, exitflag] = fminsearch(pen, p0, options);
L_est = max(0, p_est(1));
tau_est = max(eps, p_est(2));
K_est = Delta;

fprintf('Estimado: L=%.2f s, tau=%.2f s, K=%.2f °C (Delta)\n', L_est, tau_est, K_est);
fprintf('fval=%.4g, exitflag=%d\n', fval, exitflag);

% --- graficar ajuste ---
t_fine = linspace(0, t(end), 2000)';
y_fit = y_model([L_est, tau_est], t_fine);

figure;
plot(t, y, 'b.','DisplayName','Datos'); hold on;
plot(t_fine, y_fit, 'r-','LineWidth',1.6,'DisplayName','Ajuste FOPTD');
xlabel('t (s)'); ylabel('T (°C)');
legend('Location','best'); grid on;
title(sprintf('FOPTD fit: L=%.2fs, tau=%.2fs, K=%.2f', L_est, tau_est, K_est));

% marcar L en la gráfica
xline(L_est,'k--','LineWidth',1,'Label','L_{est}','LabelVerticalAlignment','bottom');

% --- imprimir algunas comprobaciones ---
% tiempo hasta 63% teórico
t63_theo = L_est + tau_est;
fprintf('t63 (teórico) = %.2f s\n', t63_theo);
