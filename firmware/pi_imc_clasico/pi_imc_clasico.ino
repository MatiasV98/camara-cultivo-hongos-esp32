#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <WiFi.h>
#include <WebServer.h>

//// ---------------- CONFIGURACIÓN WIFI AP ----------------
// Credenciales del punto de acceso: copiar config.example.h como config.h y editarlo.
// Sin config.h se usan los valores de ejemplo.
#if __has_include("config.h")
#include "config.h"
#else
#include "config.example.h"
#endif
const char* ssid = AP_SSID;
const char* password = AP_PASSWORD;

//// ---------------- PINS ----------------
const int PIN_ONEWIRE   = 4;
const int PIN_RELAY_PEL = 27;

//// ---------------- SENSORES / OBJETOS ----------------
OneWire oneWire(PIN_ONEWIRE);
DallasTemperature dsSensors(&oneWire);
WebServer server(80);

//// ---------------- PARÁMETROS CONTROLADOR PI ----------------
float Kc = 0.6;           // Ganancia proporcional (PI IMC, ensayo final)
float Ti = 2200.0;        // Tiempo integral [s]
float Ts = 2.0;           // Periodo de muestreo [s]

// Setpoint (temperatura deseada) - Configurable desde web
float setpoint = 25.0;

// Variables internas del controlador PI
float error = 0, prevError = 0;
float integral = 0;
float controlSignal = 0;

// Control por relé con periodo largo
unsigned long controlPeriod = 10000; // periodo de control 10 s
unsigned long lastControlTime = 0;
bool relayState = false;
unsigned long relayOnTime = 0;
unsigned long relayOffTime = 0;

// --- Controladores disponibles ---
enum TipoControl { PI_IMC, PI_CLASICO };
TipoControl modoControl = PI_IMC;

// Parámetros del PI clásico (cancelación del polo dominante + ajuste por LGR)
float Kc_clasico = 0.65;    // Ganancia proporcional clásico (ensayo final, informe Fig. 12)
float Ti_clasico = 2200.0;  // Tiempo integral clásico [s]

//// ---------------- VARIABLES DEL SISTEMA ----------------
bool controlActivo = false;
bool sistemaEncendido = false;
bool modoManual = false;
bool registroIniciado = false;
float temperaturaActual = 0;
float temperaturaMinima = 200;
float temperaturaMaxima = -200;
unsigned long ultimaMuestraControl = 0;
unsigned long contadorMuestras = 0;
unsigned long tiempoInicioControl = 0;

// Variables para manejo de errores del sensor
bool sensorError = false;
unsigned long ultimoIntentoSensor = 0;
const unsigned long INTERVALO_REINTENTO_SENSOR = 5000;

//// ---------------- HTML PAGINA CONTROL ----------------
const char* htmlPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>Controlador PI - Sistema Térmico</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }
        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            min-height: 100vh;
            padding: 20px;
        }
        .container {
            max-width: 900px;
            margin: 0 auto;
        }
        .header {
            text-align: center;
            margin-bottom: 30px;
            padding: 25px;
            background: rgba(255,255,255,0.15);
            border-radius: 20px;
            backdrop-filter: blur(10px);
            border: 1px solid rgba(255,255,255,0.2);
        }
        .header h1 {
            font-size: 2.2rem;
            margin-bottom: 10px;
            background: linear-gradient(45deg, #fff, #e0e7ff);
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
        }
        .card {
            background: rgba(255,255,255,0.15);
            padding: 25px;
            margin: 20px 0;
            border-radius: 20px;
            backdrop-filter: blur(10px);
            border: 1px solid rgba(255,255,255,0.2);
            box-shadow: 0 8px 32px rgba(0,0,0,0.1);
        }
        .card h2 {
            text-align: center;
            margin-bottom: 20px;
            font-size: 1.5rem;
            color: #e0e7ff;
        }
        .config-section {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 20px;
            margin: 20px 0;
        }
        .config-box {
            background: rgba(0,0,0,0.3);
            padding: 20px;
            border-radius: 15px;
            border: 1px solid rgba(255,255,255,0.1);
        }
        .config-label {
            font-size: 1rem;
            color: #a5b4fc;
            margin-bottom: 10px;
            display: block;
        }
        .config-input {
            width: 100%;
            padding: 12px;
            border: none;
            border-radius: 8px;
            background: rgba(255,255,255,0.1);
            color: white;
            font-size: 1.1rem;
            border: 1px solid rgba(255,255,255,0.2);
        }
        .status-grid {
            display: grid;
            grid-template-columns: repeat(2, 1fr);
            gap: 15px;
            margin: 20px 0;
        }
        .status-box {
            background: rgba(0,0,0,0.3);
            padding: 20px;
            border-radius: 15px;
            text-align: center;
            border: 1px solid rgba(255,255,255,0.1);
        }
        .status-label {
            font-size: 0.9rem;
            color: #a5b4fc;
            margin-bottom: 8px;
        }
        .status-value {
            font-size: 1.8rem;
            font-weight: bold;
            margin: 5px 0;
        }
        #tempActual {
            color: #4ade80;
            text-shadow: 0 0 20px rgba(74, 222, 128, 0.3);
        }
        #tempSetpoint {
            color: #f59e0b;
        }
        #tempMinima {
            color: #60a5fa;
        }
        #tempMaxima {
            color: #f87171;
        }
        .control-section {
            text-align: center;
        }
        .btn-control {
            padding: 15px 40px;
            border: none;
            border-radius: 12px;
            font-size: 1.2rem;
            font-weight: bold;
            cursor: pointer;
            transition: all 0.3s ease;
            color: white;
            box-shadow: 0 6px 20px rgba(0,0,0,0.4);
            margin: 10px;
        }
        .btn-iniciar {
            background: linear-gradient(45deg, #10b981, #34d399);
        }
        .btn-detener {
            background: linear-gradient(45deg, #ef4444, #f87171);
        }
        .btn-manual {
            background: linear-gradient(45deg, #3b82f6, #60a5fa);
        }
        .btn-manual-on {
            background: linear-gradient(45deg, #f59e0b, #fbbf24);
        }
        .btn-control:disabled {
            background: #6b7280;
            cursor: not-allowed;
            box-shadow: none;
        }
        .btn-control:hover:not(:disabled) {
            transform: translateY(-3px);
            box-shadow: 0 8px 25px rgba(0,0,0,0.3);
        }
        .indicador-estado {
            display: inline-flex;
            align-items: center;
            gap: 10px;
            margin: 15px 0;
            padding: 15px 25px;
            background: rgba(0,0,0,0.3);
            border-radius: 12px;
            font-size: 1.1rem;
        }
        .punto-estado {
            width: 14px;
            height: 14px;
            border-radius: 50%;
            animation: pulse 2s infinite;
        }
        .estado-inactivo { background: #6b7280; }
        .estado-activo { background: #10b981; animation: pulse 1s infinite; }
        .estado-calentando { background: #f59e0b; animation: pulse 1s infinite; }
        .estado-manual { background: #3b82f6; animation: pulse 1s infinite; }
        @keyframes pulse {
            0% { opacity: 1; }
            50% { opacity: 0.5; }
            100% { opacity: 1; }
        }
        .info-control {
            background: rgba(0,0,0,0.3);
            padding: 15px;
            border-radius: 10px;
            margin: 15px 0;
            text-align: left;
        }
        .parametros-pi {
            display: grid;
            grid-template-columns: repeat(3, 1fr);
            gap: 15px;
            margin: 20px 0;
        }
        .footer {
            text-align: center;
            margin-top: 30px;
            color: #a5b4fc;
        }
        .datos-control {
            background: rgba(59, 130, 246, 0.3);
            padding: 15px;
            border-radius: 10px;
            margin: 15px 0;
            text-align: center;
        }
        .relay-status {
            background: rgba(245, 158, 11, 0.3);
            padding: 15px;
            border-radius: 10px;
            margin: 15px 0;
            text-align: center;
            font-size: 1.1rem;
        }
        .manual-controls {
            display: flex;
            justify-content: center;
            gap: 15px;
            margin: 15px 0;
        }
        .btn-manual-control {
            padding: 12px 25px;
            border: none;
            border-radius: 8px;
            font-size: 1rem;
            cursor: pointer;
            transition: all 0.3s ease;
        }
        .btn-manual-on { background: #f59e0b; color: white; }
        .btn-manual-off { background: #3b82f6; color: white; }
        .selector-controlador {
            display: flex;
            justify-content: center;
            gap: 20px;
            margin: 20px 0;
        }
        .btn-selector {
            padding: 12px 25px;
            border: none;
            border-radius: 8px;
            font-size: 1rem;
            cursor: pointer;
            transition: all 0.3s ease;
            background: rgba(255,255,255,0.1);
            color: white;
            border: 2px solid transparent;
        }
        .btn-selector.active {
            border-color: #10b981;
            background: rgba(16, 185, 129, 0.3);
        }
        .error-box {
            background: rgba(239, 68, 68, 0.3);
            padding: 15px;
            border-radius: 10px;
            margin: 15px 0;
            text-align: center;
            border: 1px solid rgba(239, 68, 68, 0.5);
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>Controlador PI - Sistema Térmico</h1>
            <p>Control proporcional-integral para sistema Peltier + Reservorio 450ml</p>
        </div>
        
        <div id="errorContainer" style="display: none;">
            <div class="error-box">
                <strong>PROBLEMA DE SENSOR</strong>
                <div id="errorMessage">No se puede leer la temperatura</div>
            </div>
        </div>
        
        <div class="card">
            <h2>Configuración Controlador PI</h2>
            
            <div class="selector-controlador">
                <button class="btn-selector active" id="btnIMC" onclick="cambiarModo('imc')">
                    IMC
                </button>
                <button class="btn-selector" id="btnClasico" onclick="cambiarModo('clasico')">
                    CLÁSICO
                </button>
            </div>
            
            <div class="config-section">
                <div class="config-box">
                    <label class="config-label">Setpoint (°C)</label>
                    <input type="number" id="setpoint" class="config-input" step="0.1" value="25.0">
                </div>
                <div class="config-box">
                    <label class="config-label">Intervalo Muestreo (s)</label>
                    <input type="number" id="intervaloMuestreo" class="config-input" value="2">
                </div>
            </div>
            
            <div class="parametros-pi">
                <div class="config-box">
                    <label class="config-label">Kc - Ganancia</label>
                    <input type="number" id="Kc" class="config-input" step="0.001" value="0.6">
                </div>
                <div class="config-box">
                    <label class="config-label">Ti - Tiempo Integral (s)</label>
                    <input type="number" id="Ti" class="config-input" step="1" value="2200">
                </div>
                <div class="config-box">
                    <label class="config-label">Periodo Control (s)</label>
                    <input type="number" id="controlPeriod" class="config-input" value="10">
                </div>
            </div>
            
            <div class="info-control">
                <div><strong>Controlador Activo:</strong> <span id="controladorActivo">IMC</span></div>
                <div><strong>Parámetros IMC:</strong> Kc = 0.6, Ti = 2200s</div>
                <div><strong>Parámetros Clásico:</strong> Kc = 0.65, Ti = 2200s</div>
            </div>
        </div>

        <div class="card">
            <h2>Estado del Sistema</h2>
            
            <div class="relay-status" id="relayStatus">
                <strong>Estado Peltier: </strong><span id="textoRelay">APAGADO</span> | 
                <strong>Señal Control: </strong><span id="controlSignal">0.000</span> |
                <strong>Registro: </strong><span id="estadoRegistro">INACTIVO</span>
            </div>
            
            <div class="status-grid">
                <div class="status-box">
                    <div class="status-label">TEMPERATURA ACTUAL</div>
                    <div class="status-value" id="tempActual">--°C</div>
                    <div class="status-label">DS18B20</div>
                </div>
                <div class="status-box">
                    <div class="status-label">SETPOINT</div>
                    <div class="status-value" id="tempSetpoint">--°C</div>
                    <div class="status-label">Temperatura deseada</div>
                </div>
                <div class="status-box">
                    <div class="status-label">MÍNIMA REGISTRADA</div>
                    <div class="status-value" id="tempMinima">--°C</div>
                    <div class="status-label">Desde inicio control</div>
                </div>
                <div class="status-box">
                    <div class="status-label">MÁXIMA REGISTRADA</div>
                    <div class="status-value" id="tempMaxima">--°C</div>
                    <div class="status-label">Desde inicio control</div>
                </div>
            </div>
            
            <div class="datos-control">
                <h3>Datos del Controlador PI</h3>
                <div style="display: grid; grid-template-columns: repeat(3, 1fr); gap: 10px;">
                    <div>
                        <div class="status-label">ERROR ACTUAL</div>
                        <div class="status-value" id="errorActual">--°C</div>
                    </div>
                    <div>
                        <div class="status-label">TÉRMINO INTEGRAL</div>
                        <div class="status-value" id="terminoIntegral">--</div>
                    </div>
                    <div>
                        <div class="status-label">MUESTRAS</div>
                        <div class="status-value" id="contadorMuestras">0</div>
                    </div>
                </div>
            </div>
            
            <div class="info-control">
                <div><strong>Parámetros Actuales:</strong></div>
                <div>• <strong>Controlador:</strong> <span id="currentControlador">IMC</span></div>
                <div>• <strong>Kc:</strong> <span id="currentKc">0.6</span> | <strong>Ti:</strong> <span id="currentTi">2200</span>s</div>
                <div>• <strong>Periodo Control:</strong> <span id="currentPeriod">10</span>s | <strong>Ts:</strong> <span id="currentTs">2</span>s</div>
                <div>• <strong>Ciclo Trabajo:</strong> <span id="dutyCycle">0</span>% | <strong>Estado:</strong> <span id="estadoControl">INACTIVO</span></div>
            </div>
        </div>

        <div class="card">
            <h2>Control del Sistema</h2>
            <div class="control-section">
                <div class="indicador-estado">
                    <div class="punto-estado" id="puntoEstado"></div>
                    <span>Estado: <strong id="estadoSistema">INACTIVO</strong></span>
                </div>
                
                <button class="btn-control btn-iniciar" onclick="iniciarControl()" id="btnIniciar">
                    INICIAR CONTROL PI
                </button>
                
                <button class="btn-control btn-detener" onclick="detenerControl()" id="btnDetener" disabled>
                    ⏹️ DETENER CONTROL
                </button>
                
                <button class="btn-control btn-manual" onclick="toggleManual()" id="btnManual">
                    MODO MANUAL: OFF
                </button>

                <div class="manual-controls" id="manualControls" style="display: none;">
                    <button class="btn-manual-control btn-manual-on" onclick="setManualState(true)">
                        ENCENDER PELTIER
                    </button>
                    <button class="btn-manual-control btn-manual-off" onclick="setManualState(false)">
                        APAGAR PELTIER
                    </button>
                </div>
            </div>
        </div>

        <div class="footer">
            <div class="info-control" style="text-align: center;">
                Actualizado: <span id="updateTime">--</span> | 
                <span id="sensorStatus">Conectado</span> |
                <span id="controlMode">AUTOMÁTICO</span> |
                <span id="wifiStatus">Conectado</span>
            </div>
        </div>
    </div>

    <script>
        let modoManual = false;
        let manualState = false;
        let errorCount = 0;

        function mostrarError(mensaje) {
            const errorContainer = document.getElementById('errorContainer');
            const errorMessage = document.getElementById('errorMessage');
            errorMessage.textContent = mensaje;
            errorContainer.style.display = 'block';
        }

        function ocultarError() {
            document.getElementById('errorContainer').style.display = 'none';
            errorCount = 0;
        }

        function cambiarModo(modo) {
            fetch('/modo?tipo=' + modo)
                .then(response => response.text())
                .then(data => {
                    console.log('Modo cambiado a:', modo);
                    // Actualizar interfaz
                    const btnIMC = document.getElementById('btnIMC');
                    const btnClasico = document.getElementById('btnClasico');
                    const controladorActivo = document.getElementById('controladorActivo');
                    
                    if (modo === 'imc') {
                        btnIMC.classList.add('active');
                        btnClasico.classList.remove('active');
                        controladorActivo.textContent = 'IMC';
                    } else {
                        btnIMC.classList.remove('active');
                        btnClasico.classList.add('active');
                        controladorActivo.textContent = 'CLÁSICO';
                    }
                    
                    actualizarDatos();
                })
                .catch(err => {
                    console.log('Error cambiando modo:', err);
                    mostrarError('Error al cambiar modo de control');
                });
        }

        function actualizarDatos() {
            fetch('/data')
                .then(r => {
                    if (!r.ok) {
                        throw new Error('Error en respuesta del servidor');
                    }
                    return r.json();
                })
                .then(data => {
                    ocultarError();
                    
                    // Actualizar estado del relay
                    document.getElementById('textoRelay').textContent = data.relayState ? 'ENCENDIDO' : 'APAGADO';
                    document.getElementById('relayStatus').style.backgroundColor = data.relayState ? 
                        'rgba(245, 158, 11, 0.3)' : 'rgba(59, 130, 246, 0.3)';
                    
                    // Actualizar estado del registro
                    document.getElementById('estadoRegistro').textContent = data.registroIniciado ? 'ACTIVO' : 'INACTIVO';
                    document.getElementById('estadoRegistro').style.color = data.registroIniciado ? '#10b981' : '#6b7280';
                    
                    // Actualizar temperaturas
                    const tempActualElem = document.getElementById('tempActual');
                    const tempSetpointElem = document.getElementById('tempSetpoint');
                    const tempMinimaElem = document.getElementById('tempMinima');
                    const tempMaximaElem = document.getElementById('tempMaxima');
                    
                    if (!isNaN(data.temperatura) && data.temperatura != -127) {
                        tempActualElem.textContent = data.temperatura.toFixed(2) + '°C';
                        // Cambiar color según error
                        const error = data.setpoint - data.temperatura;
                        if (Math.abs(error) < 0.5) {
                            tempActualElem.style.color = '#10b981'; // Verde - en setpoint
                        } else if (error > 0) {
                            tempActualElem.style.color = '#f59e0b'; // Naranja - debajo del setpoint
                        } else {
                            tempActualElem.style.color = '#3b82f6'; // Azul - arriba del setpoint
                        }
                    } else {
                        tempActualElem.textContent = '--°C';
                        tempActualElem.style.color = '#ef4444'; // Rojo - error
                    }
                    
                    tempSetpointElem.textContent = data.setpoint.toFixed(1) + '°C';
                    tempMinimaElem.textContent = isFinite(data.temperaturaMinima) && data.temperaturaMinima != 200 ? 
                        data.temperaturaMinima.toFixed(2) + '°C' : '--°C';
                    tempMaximaElem.textContent = isFinite(data.temperaturaMaxima) && data.temperaturaMaxima != -200 ? 
                        data.temperaturaMaxima.toFixed(2) + '°C' : '--°C';
                    
                    // Actualizar datos del controlador
                    document.getElementById('errorActual').textContent = 
                        !isNaN(data.error) ? data.error.toFixed(2) + '°C' : '--°C';
                    document.getElementById('terminoIntegral').textContent = 
                        !isNaN(data.integral) ? data.integral.toFixed(0) : '--';
                    document.getElementById('controlSignal').textContent = 
                        !isNaN(data.controlSignal) ? data.controlSignal.toFixed(3) : '0.000';
                    document.getElementById('contadorMuestras').textContent = 
                        data.contadorMuestras;
                    document.getElementById('dutyCycle').textContent = 
                        !isNaN(data.controlSignal) ? Math.round(data.controlSignal * 100) : '0';
                    
                    // Actualizar parámetros actuales
                    document.getElementById('currentKc').textContent = data.Kc;
                    document.getElementById('currentTi').textContent = data.Ti;
                    document.getElementById('currentPeriod').textContent = data.controlPeriod / 1000;
                    document.getElementById('currentTs').textContent = data.Ts;
                    document.getElementById('currentControlador').textContent = data.modoControl == 0 ? 'IMC' : 'CLÁSICO';
                    
                    // Actualizar estado del sistema
                    const estadoSistemaElem = document.getElementById('estadoSistema');
                    const puntoEstadoElem = document.getElementById('puntoEstado');
                    const btnIniciar = document.getElementById('btnIniciar');
                    const btnDetener = document.getElementById('btnDetener');
                    const btnManual = document.getElementById('btnManual');
                    const manualControls = document.getElementById('manualControls');
                    
                    if (data.controlActivo) {
                        if (data.modoManual) {
                            estadoSistemaElem.textContent = 'MODO MANUAL';
                            puntoEstadoElem.className = 'punto-estado estado-manual';
                            btnManual.textContent = 'MODO MANUAL: ON';
                            btnManual.className = 'btn-control btn-manual-on';
                            manualControls.style.display = 'flex';
                        } else {
                            estadoSistemaElem.textContent = 'CONTROL PI ACTIVO';
                            puntoEstadoElem.className = 'punto-estado estado-activo';
                            btnManual.textContent = 'MODO MANUAL: OFF';
                            btnManual.className = 'btn-control btn-manual';
                            manualControls.style.display = 'none';
                        }
                        btnIniciar.disabled = true;
                        btnDetener.disabled = false;
                    } else {
                        estadoSistemaElem.textContent = 'INACTIVO';
                        puntoEstadoElem.className = 'punto-estado estado-inactivo';
                        btnIniciar.disabled = false;
                        btnDetener.disabled = true;
                        btnManual.textContent = 'MODO MANUAL: OFF';
                        btnManual.className = 'btn-control btn-manual';
                        manualControls.style.display = 'none';
                    }
                    
                    document.getElementById('estadoControl').textContent = 
                        data.controlActivo ? (data.modoManual ? 'MANUAL' : 'AUTOMÁTICO') : 'INACTIVO';
                    
                    // Actualizar selector de controlador según el modo actual
                    const btnIMC = document.getElementById('btnIMC');
                    const btnClasico = document.getElementById('btnClasico');
                    const controladorActivo = document.getElementById('controladorActivo');
                    
                    if (data.modoControl == 0) {
                        btnIMC.classList.add('active');
                        btnClasico.classList.remove('active');
                        controladorActivo.textContent = 'IMC';
                    } else {
                        btnIMC.classList.remove('active');
                        btnClasico.classList.add('active');
                        controladorActivo.textContent = 'CLÁSICO';
                    }
                    
                    // Actualizar hora y estado
                    document.getElementById('updateTime').textContent = new Date().toLocaleTimeString();
                    document.getElementById('sensorStatus').textContent = 
                        !isNaN(data.temperatura) && data.temperatura != -127 ? 'Sensor OK' : 'Sensor Error';
                    document.getElementById('controlMode').textContent = 
                        data.modoManual ? 'MANUAL' : 'AUTOMÁTICO';
                    document.getElementById('wifiStatus').textContent = 'Conectado';
                })
                .catch(err => {
                    console.log('Error obteniendo datos:', err);
                    errorCount++;
                    document.getElementById('sensorStatus').textContent = 'Error conexión';
                    document.getElementById('wifiStatus').textContent = 'Desconectado';
                    
                    if (errorCount > 3) {
                        mostrarError('No se puede conectar con el ESP32. Verifica la conexión WiFi.');
                    }
                });
        }

        function iniciarControl() {
            const newSetpoint = parseFloat(document.getElementById('setpoint').value) || 25.0;
            const newKc = parseFloat(document.getElementById('Kc').value) || 0.6;
            const newTi = parseFloat(document.getElementById('Ti').value) || 2200;
            const newTs = parseInt(document.getElementById('intervaloMuestreo').value) || 2;
            const newPeriod = parseInt(document.getElementById('controlPeriod').value) || 10;
            
            fetch('/iniciar?setpoint=' + newSetpoint + '&Kc=' + newKc + '&Ti=' + newTi + '&Ts=' + newTs + '&period=' + newPeriod)
                .then(() => {
                    console.log('Control PI iniciado');
                    actualizarDatos();
                })
                .catch(err => {
                    console.log('Error iniciando control:', err);
                    mostrarError('Error al iniciar el control');
                });
        }

        function detenerControl() {
            if (confirm('¿Estás seguro de que quieres detener el control?')) {
                fetch('/detener')
                    .then(() => {
                        console.log('Control detenido');
                        actualizarDatos();
                    })
                    .catch(err => {
                        console.log('Error deteniendo control:', err);
                        mostrarError('Error al detener el control');
                    });
            }
        }

        function toggleManual() {
            fetch('/toggleManual')
                .then(() => {
                    console.log('Modo manual cambiado');
                    actualizarDatos();
                })
                .catch(err => {
                    console.log('Error cambiando modo manual:', err);
                    mostrarError('Error cambiando modo manual');
                });
        }

        function setManualState(state) {
            fetch('/manual?state=' + state)
                .then(() => {
                    console.log('Estado manual cambiado:', state);
                    actualizarDatos();
                })
                .catch(err => {
                    console.log('Error cambiando estado manual:', err);
                    mostrarError('Error cambiando estado manual');
                });
        }

        // Actualizar cada 1 segundo
        setInterval(actualizarDatos, 1000);
        actualizarDatos();
    </script>
</body>
</html>
)rawliteral";

// Función mejorada de lectura del sensor
bool leerTemperatura() {
    int count = dsSensors.getDeviceCount();
    if (count == 0) {
        Serial.println("No hay sensores DS18B20 detectados");
        sensorError = true;
        temperaturaActual = NAN;
        return false;
    }

    DeviceAddress addr;
    if (!dsSensors.getAddress(addr, 0)) {
        Serial.println("No se pudo obtener dirección del sensor");
        sensorError = true;
        temperaturaActual = NAN;
        return false;
    }

    dsSensors.requestTemperatures();
    
    // Espera no bloqueante para la conversión
    unsigned long inicio = millis();
    while (millis() - inicio < 1000) { // Timeout de 1 segundo
        if (dsSensors.isConversionComplete()) {
            float t = dsSensors.getTempCByIndex(0);
            
            if (t == DEVICE_DISCONNECTED_C || t == 85.0 || t == -127.0) {
                Serial.println("Lectura inválida del sensor");
                sensorError = true;
                temperaturaActual = NAN;
                return false;
            }
            
            temperaturaActual = t;
            sensorError = false;
            
            // Actualizar min/max solo si el registro está activo
            if (registroIniciado) {
                if (temperaturaActual > temperaturaMaxima) temperaturaMaxima = temperaturaActual;
                if (temperaturaActual < temperaturaMinima) temperaturaMinima = temperaturaActual;
            }
            
            return true;
        }
        delay(10);
        server.handleClient(); // Mantener el servidor web responsive
    }
    
    Serial.println("Timeout en lectura del sensor");
    sensorError = true;
    temperaturaActual = NAN;
    return false;
}

// Controlador PI (IMC o Clásico)
void controlPI() {
    if (!controlActivo || isnan(temperaturaActual) || modoManual) return;

    error = setpoint - temperaturaActual;

    // Selección de parámetros según modo
    float Kp = (modoControl == PI_IMC) ? Kc : Kc_clasico;
    float Ti_local = (modoControl == PI_IMC) ? Ti : Ti_clasico;

    // Calcular término integral (anti-windup simple)
    integral += (error * Ts);
    if (integral > 2000) integral = 2000;
    if (integral < -2000) integral = -2000;

    // Ecuación del PI: u(t) = Kp * ( e(t) + (1/Ti) * ∫e(t)dt )
    controlSignal = Kp * (error + (1.0 / Ti_local) * integral);

    // Limitar salida a 0..1
    if (controlSignal > 1.0) controlSignal = 1.0;
    if (controlSignal < 0.0) controlSignal = 0.0;

    // Aplicar control por relé con periodo largo
    unsigned long now = millis();
    if (now - lastControlTime >= controlPeriod) {
        lastControlTime = now;
        
        unsigned long onTime = (unsigned long)(controlSignal * controlPeriod);

        // Mínimo y máximo tiempo ON para proteger el relé
        if (onTime < 1000) onTime = 0;  // Reducido de 5000 a 1000 ms
        if (onTime > controlPeriod - 1000) onTime = controlPeriod; // Reducido de 5000 a 1000 ms
        
        relayOnTime = onTime;
        relayOffTime = controlPeriod - onTime;

        // DEBUG: Información del control
        Serial.print("Control - Error: ");
        Serial.print(error, 2);
        Serial.print(" | Control Signal: ");
        Serial.print(controlSignal, 3);
        Serial.print(" | OnTime: ");
        Serial.print(onTime / 1000.0, 1);
        Serial.println("s");

        // CSV
        if (registroIniciado) {
            Serial.print((modoControl == PI_IMC) ? "IMC," : "CLASICO,");
            Serial.print((millis() - tiempoInicioControl) / 1000.0, 1);
            Serial.print(",");
            Serial.print(temperaturaActual, 2);
            Serial.print(",");
            Serial.print(setpoint, 1);
            Serial.print(",");
            Serial.print(error, 2);
            Serial.print(",");
            Serial.print(controlSignal, 3);
            Serial.print(",");
            Serial.print(integral, 0);
            Serial.print(",");
            Serial.print(relayState ? 1 : 0);
            Serial.print(",");
            Serial.println(onTime / 1000.0, 1);
        }
    }
}

void handleModoControl() {
    if (server.hasArg("tipo")) {
        String tipo = server.arg("tipo");
        if (tipo == "imc") {
            modoControl = PI_IMC;
            Serial.println("Modo de control cambiado a: IMC");
        } else if (tipo == "clasico") {
            modoControl = PI_CLASICO;
            Serial.println("Modo de control cambiado a: CLÁSICO");
        }

        server.send(200, "text/plain", "OK");
    } else {
        server.send(400, "text/plain", "Missing tipo parameter");
    }
}

// Control manual del relé
void controlManual(bool state) {
    digitalWrite(PIN_RELAY_PEL, state ? HIGH : LOW);
    relayState = state;
    controlSignal = state ? 1.0 : 0.0;
    
    // Solo registrar en modo manual si el control está activo
    if (controlActivo && registroIniciado) {
        Serial.print("MANUAL,");
        Serial.print((millis() - tiempoInicioControl) / 1000.0, 1);
        Serial.print(",");
        Serial.print(temperaturaActual, 2);
        Serial.print(",");
        Serial.print(setpoint, 1);
        Serial.print(",");
        Serial.print(error, 2);
        Serial.print(",");
        Serial.print(controlSignal, 3);
        Serial.print(",");
        Serial.print(integral, 0);
        Serial.print(",");
        Serial.print(relayState ? 1 : 0);
        Serial.print(",");
        Serial.println("MANUAL");
    }
}

// Control automático basado en PI
void controlAutomatico() {
    if (!controlActivo || modoManual) return;
    
    unsigned long now = millis();
    unsigned long tiempoEnPeriodo = now - lastControlTime;
    
    // DEBUG: Estado del relé
    static unsigned long lastDebug = 0;
    if (now - lastDebug >= 5000) { // Debug cada 5 segundos
        lastDebug = now;
        Serial.print("Relay State: ");
        Serial.print(relayState ? "ON" : "OFF");
        Serial.print(" | Tiempo en periodo: ");
        Serial.print(tiempoEnPeriodo / 1000.0, 1);
        Serial.print("s | OnTime: ");
        Serial.print(relayOnTime / 1000.0, 1);
        Serial.println("s");
    }
    
    if (relayOnTime > 0 && tiempoEnPeriodo < relayOnTime) {
        if (!relayState) {
            digitalWrite(PIN_RELAY_PEL, HIGH);
            relayState = true;
            Serial.println("Relay ACTIVADO");
        }
    } else {
        if (relayState) {
            digitalWrite(PIN_RELAY_PEL, LOW);
            relayState = false;
            Serial.println("Relay DESACTIVADO");
        }
    }
}

// Inicializar control
void iniciarControl() {
    controlActivo = true;
    sistemaEncendido = true;
    registroIniciado = true;
    modoManual = false;
    
    // Reiniciar estadísticas
    temperaturaMinima = temperaturaActual;
    temperaturaMaxima = temperaturaActual;
    contadorMuestras = 0;
    tiempoInicioControl = millis();
    
    // Reiniciar controlador PI
    error = 0;
    prevError = 0;
    integral = 0;
    controlSignal = 0;
    lastControlTime = millis();
    
    // Asegurar que el relé esté apagado al inicio
    digitalWrite(PIN_RELAY_PEL, LOW);
    relayState = false;
    
    Serial.println("\n\nINICIANDO CONTROLADOR PI");
    Serial.println("==========================================");
    Serial.printf("Setpoint: %.1f°C\n", setpoint);
    Serial.printf("Controlador: %s\n", modoControl == PI_IMC ? "IMC" : "CLÁSICO");
    Serial.printf("Parámetros - Kc: %.3f, Ti: %.0fs, Ts: %.0fs\n", 
                  modoControl == PI_IMC ? Kc : Kc_clasico,
                  modoControl == PI_IMC ? Ti : Ti_clasico, Ts);
    Serial.printf("⏰ Periodo control: %lums\n", controlPeriod);
    
    // ENCABEZADO CSV PARA PUTTY
    Serial.println("TYPE,Time(s),Temperature(C),Setpoint(C),Error(C),ControlSignal,Integral,RelayState,OnTime(s)");
    
    // Registrar primera muestra
    if (!isnan(temperaturaActual)) {
        Serial.print("INICIO,0.0,");
        Serial.print(temperaturaActual, 2);
        Serial.print(",");
        Serial.print(setpoint, 1);
        Serial.print(",0.0,0.000,0,0,0.0");
        Serial.println();
    }
}

// Detener control
void detenerControl() {
    controlActivo = false;
    sistemaEncendido = false;
    registroIniciado = false;
    modoManual = false;
    
    // Apagar Peltier
    digitalWrite(PIN_RELAY_PEL, LOW);
    relayState = false;
    
    // Registrar final
    if (!isnan(temperaturaActual)) {
        unsigned long tiempoTotal = (millis() - tiempoInicioControl) / 1000;
        Serial.print("FIN,");
        Serial.print(tiempoTotal);
        Serial.print(",");
        Serial.print(temperaturaActual, 2);
        Serial.print(",");
        Serial.print(setpoint, 1);
        Serial.print(",");
        Serial.print(error, 2);
        Serial.print(",");
        Serial.print(controlSignal, 3);
        Serial.print(",");
        Serial.print(integral, 0);
        Serial.print(",0,0.0");
        Serial.println();
    }
    
    Serial.println("==========================================");
    Serial.println("⏹️ CONTROL DETENIDO");
    Serial.printf("Temperatura final: %.2f°C\n", temperaturaActual);
    Serial.printf("Muestras recolectadas: %d\n", contadorMuestras);
    Serial.println("==========================================\n");
}

//// ---------------- SERVER HANDLERS ----------------
void handleRoot() {
    server.send(200, "text/html", htmlPage);
}

void handleData() {
    String json = "{";
    json += "\"temperatura\":" + String(temperaturaActual) + ",";
    json += "\"setpoint\":" + String(setpoint) + ",";
    json += "\"error\":" + String(error) + ",";
    json += "\"integral\":" + String(integral) + ",";
    json += "\"controlSignal\":" + String(controlSignal) + ",";
    json += "\"relayState\":" + String(relayState ? "true" : "false") + ",";
    json += "\"controlActivo\":" + String(controlActivo ? "true" : "false") + ",";
    json += "\"modoManual\":" + String(modoManual ? "true" : "false") + ",";
    json += "\"modoControl\":" + String((int)modoControl) + ",";
    json += "\"registroIniciado\":" + String(registroIniciado ? "true" : "false") + ",";
    json += "\"temperaturaMinima\":" + String(temperaturaMinima) + ",";
    json += "\"temperaturaMaxima\":" + String(temperaturaMaxima) + ",";
    json += "\"contadorMuestras\":" + String(contadorMuestras) + ",";
    json += "\"Kc\":" + String(Kc, 3) + ",";
    json += "\"Ti\":" + String(Ti, 0) + ",";
    json += "\"Ts\":" + String(Ts, 0) + ",";
    json += "\"controlPeriod\":" + String(controlPeriod);
    json += "}";
    
    server.send(200, "application/json", json);
}

void handleIniciar() {
    // Actualizar configuración
    if (server.hasArg("setpoint")) {
        setpoint = server.arg("setpoint").toFloat();
    }
    if (server.hasArg("Kc")) {
        Kc = server.arg("Kc").toFloat();
    }
    if (server.hasArg("Ti")) {
        Ti = server.arg("Ti").toFloat();
    }
    if (server.hasArg("Ts")) {
        Ts = server.arg("Ts").toFloat();
    }
    if (server.hasArg("period")) {
        controlPeriod = server.arg("period").toInt() * 1000UL;
    }
    
    Serial.printf("Configuración - Setpoint: %.1f°C, Controlador: %s, Kc: %.3f, Ti: %.0fs, Ts: %.0fs, Period: %lums\n", 
                  setpoint, modoControl == PI_IMC ? "IMC" : "CLÁSICO", Kc, Ti, Ts, controlPeriod);
    
    iniciarControl();
    server.send(200, "text/plain", "OK");
}

void handleDetener() {
    detenerControl();
    server.send(200, "text/plain", "OK");
}

void handleManual() {
    if (server.hasArg("state")) {
        bool state = server.arg("state") == "true";
        controlManual(state);
        server.send(200, "text/plain", "OK");
    } else {
        server.send(400, "text/plain", "Missing state parameter");
    }
}

void handleToggleManual() {
    modoManual = !modoManual;
    if (modoManual) {
        Serial.println("MODO MANUAL ACTIVADO");
    } else {
        Serial.println("MODO AUTOMÁTICO ACTIVADO");
        // Reiniciar integral al volver a automático
        integral = 0;
    }
    server.send(200, "text/plain", "OK");
}

void handleAutomatico() {
    controlActivo = true;
    modoManual = false;
    server.send(200, "text/plain", "OK");
}

// Función para verificar y reiniciar WiFi si es necesario
void verificarWiFi() {
    static unsigned long ultimaVerificacionWiFi = 0;
    
    if (millis() - ultimaVerificacionWiFi >= 30000) { // Verificar cada 30 segundos
        ultimaVerificacionWiFi = millis();
        
        if (WiFi.softAPgetStationNum() == 0) {
            Serial.println("No hay clientes conectados al AP");
        } else {
            Serial.println("Clientes conectados: " + String(WiFi.softAPgetStationNum()));
        }
    }
}

//// ---------------- SETUP ----------------
void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\nSISTEMA DE CONTROL PI - PELTIER + RESERVORIO 450ml");
    Serial.println("==========================================");

    // Configurar pines
    pinMode(PIN_RELAY_PEL, OUTPUT);
    digitalWrite(PIN_RELAY_PEL, LOW);

    // Inicializar sensor DS18B20
    dsSensors.begin();
    
    int devCount = dsSensors.getDeviceCount();
    Serial.print("Sensores DS18B20 detectados: ");
    Serial.println(devCount);
    
    if (devCount > 0) {
        DeviceAddress addr;
        if (dsSensors.getAddress(addr, 0)) {
            dsSensors.setResolution(addr, 12);
            Serial.println("Sensor configurado a 12 bits");
            
            // Lectura inicial del sensor
            if (leerTemperatura()) {
                Serial.print("Temperatura inicial: ");
                Serial.print(temperaturaActual);
                Serial.println("°C");
            } else {
                Serial.println("Error en lectura inicial del sensor");
            }
        } else {
            Serial.println("No se pudo obtener dirección del sensor");
        }
    } else {
        Serial.println("No se detectaron sensores DS18B20");
    }

    // Configurar WiFi
    WiFi.mode(WIFI_AP);
    if (WiFi.softAP(ssid, password)) {
        Serial.println("Access Point configurado correctamente");
    } else {
        Serial.println("Error configurando Access Point");
    }
    
    delay(2000);
    
    Serial.print("SSID: ");
    Serial.println(ssid);
    Serial.print("IP: ");
    Serial.println(WiFi.softAPIP());

    // Servidor web
    server.on("/", handleRoot);
    server.on("/data", handleData);
    server.on("/modo", handleModoControl);
    server.on("/iniciar", handleIniciar);
    server.on("/detener", handleDetener);
    server.on("/manual", handleManual);
    server.on("/toggleManual", handleToggleManual);
    server.on("/automatico", handleAutomatico);
    server.begin();
    
    Serial.println("Servidor web iniciado");
    Serial.println("\nCONTROLADORES DISPONIBLES:");
    Serial.println("• IMC: Kc = 0.6, Ti = 2200s");
    Serial.println("• CLÁSICO: Kc = 0.65, Ti = 2200s");
    Serial.println("\nSALIDA CSV PARA PUTTY (solo durante control activo):");
    Serial.println("TYPE,Time(s),Temperature(C),Setpoint(C),Error(C),ControlSignal,Integral,RelayState,OnTime(s)");
    Serial.println("\nSistema listo - Ve a la IP mostrada arriba");
    Serial.println("==========================================\n");
}

//// ---------------- LOOP PRINCIPAL ----------------
void loop() {
    server.handleClient();

    // Leer temperatura cada 2 segundos
    static unsigned long ultimaLecturaWeb = 0;
    unsigned long ahora = millis();
    
    if (ahora - ultimaLecturaWeb >= 2000) {
        ultimaLecturaWeb = ahora;
        
        if (!leerTemperatura()) {
            // Reintentar lectura del sensor si hay error
            static unsigned long ultimoReintento = 0;
            if (ahora - ultimoReintento >= INTERVALO_REINTENTO_SENSOR) {
                ultimoReintento = ahora;
                Serial.println("Reintentando lectura del sensor...");
                leerTemperatura();
            }
        }
        
        // DEBUG: Temperatura periódica
        if (!isnan(temperaturaActual)) {
            static unsigned long lastTempDebug = 0;
            if (ahora - lastTempDebug >= 10000) { // Cada 10 segundos
                lastTempDebug = ahora;
                Serial.print("Temp: ");
                Serial.print(temperaturaActual, 2);
                Serial.print("°C | Setpoint: ");
                Serial.print(setpoint, 1);
                Serial.print("°C | Error: ");
                Serial.println(setpoint - temperaturaActual, 2);
            }
        }
    }

    // Verificar estado WiFi periódicamente
    verificarWiFi();

    // Control PI y registro CSV solo cuando el control está activo
    if (controlActivo) {
        // Control PI cada Ts segundos
        if (ahora - ultimaMuestraControl >= (unsigned long)(Ts * 1000)) {
            ultimaMuestraControl = ahora;
            contadorMuestras++;
            
            // Ejecutar controlador PI
            controlPI();
            
            // Salida CSV de datos cada muestra
            if (registroIniciado && !isnan(temperaturaActual)) {
                Serial.print("DATA,");
                Serial.print((ahora - tiempoInicioControl) / 1000.0, 1);
                Serial.print(",");
                Serial.print(temperaturaActual, 2);
                Serial.print(",");
                Serial.print(setpoint, 1);
                Serial.print(",");
                Serial.print(error, 2);
                Serial.print(",");
                Serial.print(controlSignal, 3);
                Serial.print(",");
                Serial.print(integral, 0);
                Serial.print(",");
                Serial.print(relayState ? 1 : 0);
                Serial.print(",");
                Serial.println(relayOnTime / 1000.0, 1);
            }
        }

        // Ejecutar control automático (si no está en modo manual)
        if (!modoManual) {
            controlAutomatico();
        }
    }

    delay(10);
}