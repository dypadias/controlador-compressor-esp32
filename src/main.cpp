/*
  Compressor Poço - Versão Profissional EletroMatos v5.1 (Final para PlatformIO)
  -----------------------------------------------------------------------------
  Autor: Diego - EletroMatos
  Data: 20/10/2025

  FUNCIONALIDADES DESTA VERSÃO:
  - CORREÇÃO DE COMPILAÇÃO: Substituído o método obsoleto "server.send(SPIFFS,...)"
    pelo método correto "server.streamFile(...)", resolvendo o erro de compilação.
  - ARQUITETURA PROFISSIONAL: Código C++ limpo, HTML servido via SPIFFS.
  - Mantém todas as lógicas e correções das versões anteriores.
*/

// ==================== BIBLIOTECAS ====================
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <SPIFFS.h>

// ==================== ESTRUTURAS DE DADOS ====================
struct EnchimentoInfo {
  unsigned long tempo;
  unsigned int ciclosParciais;
};

// ==================== CONFIGURAÇÕES GERAIS ====================
const char* httpUser = "admin";
const char* httpPass = "1234";
const char* apSsid = "EletroMatos_Compressor";
const char* apPassword = "12345678";

WebServer server(80);
DNSServer dnsServer;
Preferences preferences;

// ==================== PINOS ====================
const int PINO_RELE_COMPRESSOR = 26;
const int ENTRADA_CAIXA_CHEIA = 25;
const int PINO_SENSOR_TEMPERATURA = 4;
const int LED_STATUS = 2;

// ==================== SENSOR DE TEMPERATURA ====================
bool sensorEnabled = true;
OneWire oneWire(PINO_SENSOR_TEMPERATURA);
DallasTemperature sensors(&oneWire);

// ==================== PARÂMETROS DE OPERAÇÃO ====================
unsigned long tempoLigado = 600000UL;
unsigned long tempoDescanso = 100000UL;
float temperaturaMaxima = 60.0;

// ==================== VARIÁVEIS DE EXECUÇÃO ====================
bool compressorLigado = false;
bool modoManual = false;
bool caixaCheia = false;
float temperaturaAtual = 25.0;
unsigned long ciclosParciaisOperacao = 0UL;
unsigned long ultimoTempoControle = 0UL;
unsigned long ultimoSaveMillis = 0UL;
const unsigned long SAVE_INTERVAL = 60000UL;
unsigned long inicioCicloMillis = 0;
unsigned long ultimoCheckWiFi = 0;
const long intervaloCheckWiFi = 30000;
int tentativasReconexao = 0;
const int maxTentativasReconexao = 5;

unsigned long inicioCicloEnchimentoMillis = 0;
bool desligadoPorTemperaturaAlta = false;
const int TAMANHO_HISTORICO_ENCHIMENTO = 5;
EnchimentoInfo historicoEnchimento[TAMANHO_HISTORICO_ENCHIMENTO];
int indiceHistoricoEnchimento = 0;
unsigned long ciclosEnchimentoCompletos = 0UL;
unsigned int ciclosParciaisNesteEnchimento = 0;

float historicoTemp[24];
int indiceHistorico = 0;
unsigned long ultimaLeituraGrafico = 0;
const long INTERVALO_GRAFICO = 3600000UL;

// ==================== PROTÓTIPOS DAS FUNÇÕES ====================
bool autenticar();
void handleRoot();
void handleLigar();
void handleDesligar();
void handleAutomatico();
void handleStatus();
void handleConfig();
void handleZerarCiclos();
void handleTempData();
void handleConfigWiFi();
void handleSalvarWiFi();
String paginaConfigWiFi();
void gerenciarConexaoWiFi();
void registrarTemperatura();
void configurarRotasWebNormais();
void ligarCompressor();
void desligarCompressor();
void carregarConfiguracoesOperacao();
void salvarConfiguracoesOperacao();
void atualizarSensores();
void controleAutomatico();
bool conectarWiFi();
void iniciarModoAutonomo();

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println(F("\n\n=== Iniciando EletroMatos Compressor v5.1 ==="));
  
  if(!SPIFFS.begin(true)){
    Serial.println("Ocorreu um erro ao montar o SPIFFS");
    return;
  }

  for (int i = 0; i < 24; i++) { historicoTemp[i] = -1000; }
  memset(historicoEnchimento, 0, sizeof(historicoEnchimento));

  pinMode(PINO_RELE_COMPRESSOR, OUTPUT);
  digitalWrite(PINO_RELE_COMPRESSOR, HIGH);
  pinMode(ENTRADA_CAIXA_CHEIA, INPUT_PULLUP);
  pinMode(LED_STATUS, OUTPUT);
  digitalWrite(LED_STATUS, LOW);

  preferences.begin("compressor", false);
  carregarConfiguracoesOperacao();

  if (sensorEnabled) { sensors.begin(); }

  if (!conectarWiFi()) {
    iniciarModoAutonomo();
  } else {
    configurarRotasWebNormais();
    server.begin();
    Serial.println(F("✅ Conectado à rede do cliente. Servidor HTTP iniciado."));
    if (MDNS.begin("compressor")) {
      Serial.println(F("✅ Servidor mDNS iniciado. Acesse em http://compressor.local"));
    } else {
      Serial.println(F("❌ Erro ao iniciar mDNS."));
    }
  }

  ultimoTempoControle = millis();
  Serial.println(F("✅ Sistema pronto."));
}

// ==================== LOOP PRINCIPAL ====================
void loop() {
  if (WiFi.getMode() == WIFI_AP) { dnsServer.processNextRequest(); }
  else { gerenciarConexaoWiFi(); }
  server.handleClient();
  atualizarSensores();
  registrarTemperatura();
  if (!modoManual) { controleAutomatico(); }
  if (WiFi.getMode() == WIFI_AP) { digitalWrite(LED_STATUS, (millis() / 500) % 2); }
  else {
    if (WiFi.status() != WL_CONNECTED) { digitalWrite(LED_STATUS, (millis() / 200) % 2); }
    else { digitalWrite(LED_STATUS, compressorLigado ? HIGH : LOW); }
  }
  vTaskDelay(10 / portTICK_PERIOD_MS);
}

// ==================== LÓGICA DE CONTROLE DO RELÉ ====================
void ligarCompressor() {
  if (!compressorLigado && !caixaCheia) {
    digitalWrite(PINO_RELE_COMPRESSOR, LOW);
    compressorLigado = true;
    inicioCicloMillis = millis();
    if (inicioCicloEnchimentoMillis == 0 && !caixaCheia) {
      inicioCicloEnchimentoMillis = millis();
      ciclosParciaisNesteEnchimento = 0;
      Serial.println(F("💧 Iniciando novo ciclo de enchimento (disparo por compressor)."));
    }
    ciclosParciaisNesteEnchimento++;
    ciclosParciaisOperacao++;
    Serial.printf("⚡️ Ciclo parcial #%u iniciado. Total de ciclos: %lu\n", ciclosParciaisNesteEnchimento, ciclosParciaisOperacao);
    Serial.println(F("🟢 COMPRESSOR LIGADO"));
  }
}
void desligarCompressor() {
  if (compressorLigado) {
    digitalWrite(PINO_RELE_COMPRESSOR, HIGH);
    compressorLigado = false;
    inicioCicloMillis = 0;
    Serial.println(F("🔴 COMPRESSOR DESLIGADO"));
  }
}

// ==================== LÓGICA DO GRÁFICO ====================
void registrarTemperatura() {
  unsigned long agora = millis();
  if (agora - ultimaLeituraGrafico >= INTERVALO_GRAFICO) {
    ultimaLeituraGrafico = agora;
    historicoTemp[indiceHistorico] = temperaturaAtual;
    indiceHistorico = (indiceHistorico + 1) % 24;
    Serial.println("📊 Temperatura registrada para o gráfico.");
  }
}
void handleTempData() {
  String labels = "[";
  String dados = "[";
  int leiturasValidas = 0;
  for (int i = 0; i < 24; i++) {
    int index = (indiceHistorico + i) % 24;
    if (historicoTemp[index] > -999) {
      if (leiturasValidas > 0) { labels += ","; dados += ","; }
      labels += "\"-" + String(23 - i) + "h\"";
      dados += String(historicoTemp[index], 1);
      leiturasValidas++;
    }
  }
  labels += "]";
  dados += "]";
  String json = "{\"labels\":" + labels + ",\"dados\":" + dados + "}";
  server.send(200, "application/json", json);
}

// ==================== LÓGICA DE REDE ====================
void gerenciarConexaoWiFi() {
  if (millis() - ultimoCheckWiFi > intervaloCheckWiFi) {
    ultimoCheckWiFi = millis();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("❌ Conexão WiFi perdida. Tentando reconectar...");
      tentativasReconexao++;
      if (tentativasReconexao > maxTentativasReconexao) {
        Serial.println("‼️ Falha ao reconectar. Reiniciando o sistema...");
        delay(1000);
        ESP.restart();
      }
      WiFi.disconnect();
      WiFi.reconnect();
    } else {
      if (tentativasReconexao > 0) {
        Serial.println("✅ Conexão WiFi restabelecida!");
        tentativasReconexao = 0;
      }
    }
  }
}
bool conectarWiFi() {
  String ssid = preferences.getString("wifi_ssid", "");
  if (ssid.length() == 0) return false;
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), preferences.getString("wifi_pass", "").c_str());
  Serial.print(F("📡 Conectando à rede do cliente: "));
  Serial.print(ssid);
  int tentativas = 0;
  while (WiFi.status() != WL_CONNECTED && tentativas < 20) {
    Serial.print(".");
    delay(500);
    tentativas++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(F("\n✅ Conectado!"));
    Serial.print(F("   Endereço IP (DHCP): "));
    Serial.println(WiFi.localIP());
    return true;
  } else {
    Serial.println(F("\n❌ Falha ao conectar. Ativando modo autônomo."));
    WiFi.disconnect();
    return false;
  }
}
void iniciarModoAutonomo() {
  Serial.println(F("🔧 Iniciando Modo Autônomo (Ponto de Acesso)."));
  WiFi.softAP(apSsid, apPassword);
  IPAddress apIP(192, 168, 4, 1);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  dnsServer.start(53, "*", apIP);
  server.on("/", HTTP_GET, handleConfigWiFi);
  server.on("/configwifi", HTTP_GET, handleConfigWiFi);
  server.on("/salvarwifi", HTTP_POST, handleSalvarWiFi);
  server.onNotFound(handleConfigWiFi);
  server.begin();
  Serial.print(F("   Rede criada: "));
  Serial.println(apSsid);
  Serial.print(F("   Acesse http://"));
  Serial.print(apIP);
  Serial.println(F(" para configurar a rede WiFi."));
}

// ==================== PREFERENCES E SENSORES ====================
void carregarConfiguracoesOperacao() {
  tempoLigado = preferences.getULong("tempoLigado", tempoLigado);
  tempoDescanso = preferences.getULong("tempoDescanso", tempoDescanso);
  temperaturaMaxima = preferences.getFloat("tempMaxima", temperaturaMaxima);
  ciclosParciaisOperacao = preferences.getULong("ciclosParc", 0);
  ciclosEnchimentoCompletos = preferences.getULong("ciclosEnch", 0);
  indiceHistoricoEnchimento = preferences.getInt("idxHEnch", 0);
  size_t loaded = preferences.getBytes("hEnchimento", historicoEnchimento, sizeof(historicoEnchimento));
  Serial.println(F("--- Carregando Histórico de Enchimento ---"));
  if (loaded > 0) {
    for (int i = 0; i < TAMANHO_HISTORICO_ENCHIMENTO; i++) {
      Serial.printf("  Índice %d: Tempo=%lu s, Ciclos=%u\n", i, historicoEnchimento[i].tempo, historicoEnchimento[i].ciclosParciais);
    }
  } else {
    Serial.println(F("  Nenhum histórico encontrado."));
  }
  Serial.println(F("------------------------------------------"));
  Serial.println(F("🔁 Configurações de operação carregadas."));
}
void salvarConfiguracoesOperacao() {
  unsigned long now = millis();
  if (now - ultimoSaveMillis < SAVE_INTERVAL && ultimoSaveMillis != 0) return;
  preferences.putULong("tempoLigado", tempoLigado);
  preferences.putULong("tempoDescanso", tempoDescanso);
  preferences.putFloat("tempMaxima", temperaturaMaxima);
  preferences.putULong("ciclosParc", ciclosParciaisOperacao);
  preferences.putULong("ciclosEnch", ciclosEnchimentoCompletos);
  preferences.putInt("idxHEnch", indiceHistoricoEnchimento);
  preferences.putBytes("hEnchimento", historicoEnchimento, sizeof(historicoEnchimento));
  Serial.println(F("--- Salvando Histórico de Enchimento ---"));
  for (int i = 0; i < TAMANHO_HISTORICO_ENCHIMENTO; i++) {
    Serial.printf("  Índice %d: Tempo=%lu s, Ciclos=%u\n", i, historicoEnchimento[i].tempo, historicoEnchimento[i].ciclosParciais);
  }
  Serial.println(F("----------------------------------------"));
  ultimoSaveMillis = now;
  Serial.println(F("💾 Configurações de operação salvas."));
}

void atualizarSensores() {
  if (sensorEnabled) {
    sensors.requestTemperatures();
    float t = sensors.getTempCByIndex(0);
    if (t != DEVICE_DISCONNECTED_C) { temperaturaAtual = t; }
    else {
      desligarCompressor();
      Serial.println(F("⚠️ ERRO DE SENSOR! Compressor desligado por segurança."));
    }
  }
  bool caixaEstavaCheia = caixaCheia;
  caixaCheia = !digitalRead(ENTRADA_CAIXA_CHEIA);
  if (caixaEstavaCheia && !caixaCheia) {
    inicioCicloEnchimentoMillis = millis();
    ciclosParciaisNesteEnchimento = 0;
    Serial.println(F("💧 Caixa vazia detectada. Cronômetro de enchimento INICIADO."));
  }
  if (!caixaEstavaCheia && caixaCheia && inicioCicloEnchimentoMillis > 0) {
    unsigned long tempoTotalSecs = (millis() - inicioCicloEnchimentoMillis) / 1000UL;
    historicoEnchimento[indiceHistoricoEnchimento].tempo = tempoTotalSecs;
    historicoEnchimento[indiceHistoricoEnchimento].ciclosParciais = ciclosParciaisNesteEnchimento;
    indiceHistoricoEnchimento = (indiceHistoricoEnchimento + 1) % TAMANHO_HISTORICO_ENCHIMENTO;
    ciclosEnchimentoCompletos++;
    Serial.printf("✅ Caixa Cheia! Tempo total: %lu s, em %u ciclos parciais.\n", tempoTotalSecs, ciclosParciaisNesteEnchimento);
    Serial.println(F("⏰ Cronômetro de enchimento PARADO."));
    salvarConfiguracoesOperacao();
    inicioCicloEnchimentoMillis = 0;
    if(!modoManual){
      desligarCompressor();
      ultimoTempoControle = millis();
      Serial.println("⏰ Forçando ciclo de descanso completo.");
    }
  }
  if (temperaturaAtual >= temperaturaMaxima && compressorLigado) {
    Serial.printf("‼️ DESLIGAMENTO DE EMERGÊNCIA! Temp (%.1fC) >= Limite (%.1fC).\n", temperaturaAtual, temperaturaMaxima);
    desligadoPorTemperaturaAlta = true;
    desligarCompressor();
  }
  if (caixaCheia && compressorLigado) { 
    desligarCompressor(); 
  }
}

void controleAutomatico() {
  unsigned long agora = millis();
  bool condicoesSeguras = false;
  if(desligadoPorTemperaturaAlta) {
      if(temperaturaAtual < (temperaturaMaxima - 5.0)){
          condicoesSeguras = true;
          desligadoPorTemperaturaAlta = false;
          Serial.println("🌡️ Temperatura baixou o suficiente. Sistema liberado para religar.");
      }
  } else { condicoesSeguras = (temperaturaAtual < temperaturaMaxima); }
  if (caixaCheia) { condicoesSeguras = false; }
  if (compressorLigado) {
    if (agora - ultimoTempoControle >= tempoLigado) {
      desligarCompressor();
      ultimoTempoControle = agora;
      salvarConfiguracoesOperacao();
    }
  } else {
    if ((agora - ultimoTempoControle >= tempoDescanso) && condicoesSeguras) {
      ligarCompressor();
      ultimoTempoControle = agora;
    }
  }
}

// ==================== WEB SERVER - ROTAS E HANDLERS ====================
bool autenticar() {
  if (!server.authenticate(httpUser, httpPass)) {
    server.requestAuthentication();
    return true;
  }
  return false;
}

void configurarRotasWebNormais() {
  server.on("/", HTTP_GET, []() { if (autenticar()) return; handleRoot(); });
  server.on("/ligar", HTTP_GET, []() { if (autenticar()) return; handleLigar(); });
  server.on("/desligar", HTTP_GET, []() { if (autenticar()) return; handleDesligar(); });
  server.on("/automatico", HTTP_GET, []() { if (autenticar()) return; handleAutomatico(); });
  server.on("/status", HTTP_GET, []() { if (autenticar()) return; handleStatus(); });
  server.on("/config", HTTP_POST, []() { if (autenticar()) return; handleConfig(); });
  server.on("/zerarciclos", HTTP_GET, []() { if (autenticar()) return; handleZerarCiclos(); });
  server.on("/configwifi", HTTP_GET, handleConfigWiFi);
  server.on("/tempdata", HTTP_GET, []() { if (autenticar()) return; handleTempData(); });
  server.onNotFound([]() { 
    File file = SPIFFS.open("/index.html", "r");
    if (file) {
      server.streamFile(file, "text/html");
      file.close();
      return;
    }
    server.send(404, "text/plain", "Página não encontrada.");
  });
}

void handleRoot() {
  File file = SPIFFS.open("/index.html", "r");
  if (!file) {
    server.send(404, "text/plain", "ERRO: index.html não encontrado no SPIFFS");
    return;
  }
  server.streamFile(file, "text/html");
  file.close();
}

void handleLigar() {
  if (caixaCheia) { server.send(200, "text/plain", "❌ Ação bloqueada: A caixa de água já está cheia."); return; }
  if (desligadoPorTemperaturaAlta) {
    if (temperaturaAtual >= (temperaturaMaxima - 5.0)) { server.send(200, "text/plain", "❌ Ação bloqueada: Aguardando temperatura baixar para religar (cooldown)."); return; }
  } else {
    if (temperaturaAtual >= temperaturaMaxima) { server.send(200, "text/plain", "❌ Ação bloqueada: A temperatura está acima do limite permitido."); return; }
  }
  modoManual = true;
  ligarCompressor();
  server.send(200, "text/plain", "✅ Compressor ligado manualmente.");
}

void handleDesligar() { 
  modoManual = true; 
  desligarCompressor(); 
  server.send(200, "text/plain", "OK"); 
}

void handleAutomatico() { 
  modoManual = false; 
  ultimoTempoControle = millis(); 
  desligadoPorTemperaturaAlta = false; 
  server.send(200, "text/plain", "OK"); 
}

void handleStatus() {
  unsigned long tempoRestante = 0;
  String proximoEstado = "N/A";
  if (!modoManual) {
    unsigned long agora = millis();
    unsigned long tempoDecorrido = agora - ultimoTempoControle;
    if (compressorLigado) {
      proximoEstado = "Desligar";
      if (tempoDecorrido < tempoLigado) { tempoRestante = (tempoLigado - tempoDecorrido) / 1000UL; }
    } else {
      proximoEstado = "Ligar";
      if (tempoDecorrido < tempoDescanso) { tempoRestante = (tempoDescanso - tempoDecorrido) / 1000UL; }
    }
  }
  unsigned long somaTempos = 0;
  int temposValidos = 0;
  for (int i = 0; i < TAMANHO_HISTORICO_ENCHIMENTO; i++) {
    if (historicoEnchimento[i].tempo > 0) {
      somaTempos += historicoEnchimento[i].tempo;
      temposValidos++;
    }
  }
  unsigned long mediaEnchimento = (temposValidos > 0) ? (somaTempos / temposValidos) : 0;
  String json = "{";
  json += "\"compressorLigado\":" + String(compressorLigado ? "true" : "false") + ",";
  json += "\"temperatura\":" + String(temperaturaAtual, 1) + ",";
  json += "\"caixaCheia\":" + String(caixaCheia ? "true" : "false") + ",";
  json += "\"modoManual\":" + String(modoManual ? "true" : "false") + ",";
  json += "\"alertaTemperatura\":" + String((temperaturaAtual >= temperaturaMaxima) ? "true" : "false") + ",";
  json += "\"alertaCaixaCheia\":" + String(caixaCheia ? "true" : "false") + ",";
  json += "\"ciclosParciaisOperacao\":" + String(ciclosParciaisOperacao) + ",";
  json += "\"ciclosEnchimentoCompletos\":" + String(ciclosEnchimentoCompletos) + ",";
  json += "\"tempoLigado\":" + String(tempoLigado / 60000UL) + ",";
  json += "\"tempoDescanso\":" + String(tempoDescanso / 60000UL) + ",";
  json += "\"temperaturaMaxima\":" + String(temperaturaMaxima, 1) + ",";
  json += "\"tempoRestante\":" + String(tempoRestante) + ",";
  json += "\"proximoEstado\":\"" + proximoEstado + "\",";
  json += "\"historicoEnchimento\":[";
  for (int i = 0; i < TAMANHO_HISTORICO_ENCHIMENTO; i++) {
    int index = (indiceHistoricoEnchimento - 1 - i + TAMANHO_HISTORICO_ENCHIMENTO) % TAMANHO_HISTORICO_ENCHIMENTO;
    json += "{";
    json += "\"tempo\":" + String(historicoEnchimento[index].tempo) + ",";
    json += "\"ciclos\":" + String(historicoEnchimento[index].ciclosParciais);
    json += "}";
    if (i < TAMANHO_HISTORICO_ENCHIMENTO - 1) json += ",";
  }
  json += "],";
  json += "\"mediaEnchimento\":" + String(mediaEnchimento);
  json += "}";
  server.send(200, "application/json", json);
}

void handleConfig() {
  bool changed = false;
  if (server.hasArg("tempoligado")) {
    unsigned long v = server.arg("tempoligado").toInt() * 60000UL;
    if (v >= 60000UL) { tempoLigado = v; changed = true; }
  }
  if (server.hasArg("tempodescanso")) {
    unsigned long v = server.arg("tempodescanso").toInt() * 60000UL;
    if (v >= 1000UL) { tempoDescanso = v; changed = true; }
  }
  if (server.hasArg("temperaturamax")) {
    float v = server.arg("temperaturamax").toFloat();
    if (v > 0.0) { temperaturaMaxima = v; changed = true; }
  }
  if (changed) { salvarConfiguracoesOperacao(); server.send(200, "text/plain", "✅ Configurações salvas!"); }
  else { server.send(200, "text/plain", "ℹ️ Nenhuma alteração válida."); }
}

void handleZerarCiclos() {
  ciclosParciaisOperacao = 0;
  ciclosEnchimentoCompletos = 0;
  memset(historicoEnchimento, 0, sizeof(historicoEnchimento));
  indiceHistoricoEnchimento = 0;
  salvarConfiguracoesOperacao();
  Serial.println("🔄 Contadores e histórico de enchimento zerados pelo usuário.");
  server.send(200, "text/plain", "Todos os contadores e o histórico foram zerados!");
}

String paginaConfigWiFi() {
  String html = R"rawliteral(
<!DOCTYPE html><html><head><title>Configurar WiFi</title><meta name="viewport" content="width=device-width, initial-scale=1.0"><style>body{font-family: Arial, sans-serif; background: #f4f4f4; margin: 0; padding: 20px;} .container{max-width: 500px; margin: auto; background: #fff; padding: 20px; border-radius: 8px; box-shadow: 0 0 10px rgba(0,0,0,0.1);} h1{text-align: center; color: #333;} label{display: block; margin-top: 15px; font-weight: bold;} input[type=text], input[type=password]{width: calc(100% - 22px); padding: 10px; border: 1px solid #ddd; border-radius: 4px;} button{background: #007bff; color: #fff; padding: 12px 20px; border: none; border-radius: 4px; cursor: pointer; width: 100%; font-size: 16px; margin-top: 20px;} button:hover{background: #0056b3;}</style></head><body><div class="container"><h1>Configurar Conexão WiFi</h1><p style="text-align:center;color:#666;">Use esta página para conectar o compressor à sua rede WiFi.</p><form action="/salvarwifi" method="POST"><label for="ssid">Nome da Rede (SSID):</label><input type="text" id="ssid" name="ssid" required><label for="pass">Senha da Rede:</label><input type="password" id="pass" name="pass"><button type="submit">Salvar e Reiniciar</button></form></div></body></html>)rawliteral";
  return html;
}

void handleConfigWiFi() { 
  server.send(200, "text/html", paginaConfigWiFi()); 
}

void handleSalvarWiFi() {
  Serial.println("Salvando novas configurações de rede...");
  preferences.putString("wifi_ssid", server.arg("ssid"));
  preferences.putString("wifi_pass", server.arg("pass"));
  String html = R"rawliteral(
<!DOCTYPE html><html><head><title>Configuração Salva</title><meta name="viewport" content="width=device-width, initial-scale=1.0"><style>body{font-family: Arial, sans-serif; text-align: center; padding: 50px;} .msg{font-size: 1.2em; color: #155724; background: #d4edda; padding: 20px; border-radius: 8px;}</style></head><body><div class="msg"><h1>Configurações Salvas!</h1><p>O dispositivo irá reiniciar em 5 segundos para tentar se conectar à nova rede.</p></div></body></html>)rawliteral";
  server.send(200, "text/html", html);
  delay(5000);
  ESP.restart();
}