%% estimar_FOPTD_y_plot.m
% Estima K, L, tau (FOPTD) a partir de un CSV limpio con columnas:
% Tiempo_s, Temperatura_C [, Fase]
% Luego grafica datos, ajuste y residuos.
% Requiere Optimization Toolbox para lsqnonlin (si no hay, usa fminsearch fallback).

clear; close all; clc;

%% --------- CONFIG ----------
fname = '../datos/identificacion/prueba3_240min.csv';   % ensayo a analizar
use_plot_residuals = true;
%% --------------------------

% --- leer tabla (intenta con/ sin encabezado) ---
T = readtable(fname);
% Aceptar nombres flexibles
if any(strcmpi(T.Properties.VariableNames,'Tiempo_s'))
    t = T.Tiempo_s;
elseif any(strcmpi(T.Properties.VariableNames,'Time_s'))
    t = T.Time_s;
elseif any(strcmpi(T.Properties.VariableNames,'Tiempo'))
    t = T.Tiempo;
else
    t = T{:,1};
end

if any(strcmpi(T.Properties.VariableNames,'Temperatura_C'))
    y = T.Temperatura_C;
elseif any(strcmpi(T.Properties.VariableNames,'Temperature_C'))
    y = T.Temperature_C;
else
    y = T{:,2};
end

% limpiar NaN
ok = ~isnan(t) & ~isnan(y);
t = t(ok);
y = y(ok);

% asegurar columnas como vectores columna
t = t(:);
y = y(:);

% --- valores iniciales y señales ---
T0 = mean(y(1:min(5,end)));
Tinf = mean(y(max(1,end-60):end));
Delta = Tinf - T0;

% Modelo paramétrico con K libre: p = [K, L, tau]
y_modelP = @(p,tt) (tt < p(2)).*T0 + (tt >= p(2)).*( T0 + p(1).*(1 - exp(-(tt - p(2))./p(3))) );

% residuo (solo vector)
resid = @(p) ( y - y_modelP(p, t) );

% heurísticas para iniciales
K0 = max(1e-6, Delta);
dy = [0; diff(y)];
thr = 0.002 * max(abs(dy));
idxc = find(abs(smooth(dy,5)) > thr, 1);
L0 = 0;
if ~isempty(idxc), L0 = t(max(1,idxc)); end
idx63 = find(y >= T0 + 0.632*K0, 1);
if ~isempty(idx63), tau0 = max(1, t(idx63) - L0); else tau0 = max(10, (t(end)-L0)/4); end

p0 = [K0, L0, tau0];

% bounds
lb = [0, 0, 1e-3];
ub = [5*abs(K0)+10, max(t), 1e7];

% opciones lsqnonlin (sin JacobPattern problemático)
opts = optimoptions('lsqnonlin', 'Display','iter', 'MaxIter',2000, ...
    'TolFun',1e-8, 'TolX',1e-8, 'FiniteDifferenceType','central');

% --- intentar varias inicializaciones y quedarse con la mejor ---
p0_list = [
    p0;
    [K0, 0, tau0];
    [K0, 50, max(1,tau0/2)];
    [K0, 150, max(1,tau0*2)];
    [K0*0.8, L0, tau0];
    [K0*1.2, L0, tau0]
    ];

best_resnorm = Inf;
best_p = [];
best_info = struct();

for k = 1:size(p0_list,1)
    p0k = p0_list(k,:);
    try
        [p_est, resnorm, residual, exitflag, output, lambda, J] = ...
            lsqnonlin(resid, p0k, lb, ub, opts);
    catch ME
        % fallback: fminsearch (minimiza suma de cuadrados)
        warning('lsqnonlin falló en p0 = [%g %g %g]: %s', p0k, ME.message);
        pen = @(p) sum((y - y_modelP(abs(p), t)).^2);
        p_est = fminsearch(pen, p0k);
        resnorm = sum((y - y_modelP(p_est, t)).^2);
        residual = y - y_modelP(p_est, t);
        exitflag = NaN;
        J = [];
        output = struct('message','fminsearch fallback');
    end

    if resnorm < best_resnorm
        best_resnorm = resnorm;
        best_p = p_est;
        best_info.resnorm = resnorm;
        best_info.exitflag = exitflag;
        best_info.output = output;
        best_info.J = J;
        best_info.residual = residual;
        best_info.p0 = p0k;
    end
end

% resultados finales
p_est = best_p;
K_est = p_est(1);
L_est = p_est(2);
tau_est = p_est(3);

fprintf('\nMejor estimación (de %d intentos):\n', size(p0_list,1));
fprintf('K = %.4f  , L = %.3f s  , tau = %.3f s\n', K_est, L_est, tau_est);
fprintf('resnorm = %.6g, exitflag = %g\n', best_info.resnorm, best_info.exitflag);

