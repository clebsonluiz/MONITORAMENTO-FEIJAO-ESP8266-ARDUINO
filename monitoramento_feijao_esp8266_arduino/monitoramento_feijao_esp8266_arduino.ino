/**
 * Autor: Clébson Luiz 
 * Descrição: Projeto muito simples para monitoramento de porção de terra usando arduino, 
 *            com os modulos de leitura de umidade do solo, umidade e temperadura do ar, 
 *            e acionamento de relé para ligar a irrigação por uma bomba d'agua. Upando os
 *            dados coletados com um modulo wifi esp8266, ou conhecida tambem como ESP-01.
 **/

#include <DHT.h>
#include <SoftwareSerial.h>


/**
 * defindo pinos e algumas das constantes usadas
 **/
#define DEBUG_REDE true
#define DEBUG_SENSORES false

#define pinUmidSolo A0

#define pinRX 10
#define pinTX 11

#define pinLedVermelho 7 // pin para o led vermelho
#define pinLedVerde 6 // pin para o led verde

#define pinDHT 2
#define pinReleBomba 4


const int PRONTO = 0,
          ESPERAR = 1,
          TERMINAR = 2; //Estados

// Cores do led
const int LED_OFF[] = {0, 0}, LED_RED[] = {255, 100}, LED_ORANGE[] = {130, 254}, LED_GREEN[] = {100, 250};

/*
 * Configurações da rede wifi
 **/
const String NOME_WIFI = "exemplo";
const String PASSWORD = "senha123";

/* 
 * Host usado para enviar os dados via Put request
 **/
const String HOST = "192.168.1.112";
const int PORT = 8000;
/*
 * variaveis
 **/
String resposta = "";
bool asConn = false;

int valor_analogico = 0;
float porcentUmidSolo = 0;
float tempAr = 0, umidAr = 0;


/*
 * Classe cronometro para controlar o timer sem conjelar o programa o tempo todo.
 **/
class Cronometro
{
  private:
    unsigned long tempoAnterior;
    unsigned long tempoDuracao;

  public:
    Cronometro(long duracao) { // Construtor
      tempoAnterior = 0;
      Start(duracao);
    }

    Cronometro() { // Construtor 
      tempoAnterior = 0;
      Cronometro(0);
    }

    void Start(long duracaoSeg) {
      /**
       * Define o tempo de duracao do cronometro.
       **/
      tempoDuracao = duracaoSeg * 1000;
    }

    void Reset() {
      /**
       * reseta o tempo anterior para iniciar uma nova contagem.
       **/
      tempoAnterior = millis();
    }

    long getDuration() {
      /**
       * @return tempo de duracao do cronometro em millisegundos
       **/
      return tempoDuracao;
    }

    boolean IsEncerrado() {
      /**
       * Verifica se o tempo definido foi encerrado
       **/
      unsigned long currentTime = millis();
      if (tempoAnterior <= 0) tempoAnterior = currentTime;
      return ((currentTime - tempoAnterior) > tempoDuracao);
    }
};
/**
  * classe para controlar o acionamento da bomba d'agua.
  **/
class BombaDAgua {

  private:
    int acaoBomba = PRONTO;
    int pinRele;
    boolean _acionarBomba = false;

    Cronometro timerDuracaoBomba;
    Cronometro timerEsperaBomba;

  public:
    BombaDAgua(int pinNum) { //Construtor
      pinRele = pinNum;
    }

    void init() {
      /**
       * inicia o pino do relé
       **/
      pinMode(pinRele, OUTPUT);
    }

    void proximaAcao(int acao) {
      /**
       * Define a proxima acao da bomba.
       **/
      acaoBomba = acao;
    }

    boolean IsAcao(int acao) {
      /**
       * @return verifica qual a atual ação
       **/
      return (acaoBomba == acao);
    }

    void prepararLigarBomba(int duracaoSeg, int esperarSeg) {
      /**
       * Prepara para ligar a bomba dagua definidno a duracao e quanto 
       * tempo deve esperar para ligar novamente desde a ultima irrigação
       **/
      _acionarBomba = true;
      timerDuracaoBomba.Start(duracaoSeg);
      timerEsperaBomba.Start(esperarSeg);
    }

    void ligar() {
      /**
       * Aciona o relé da bomba d'agua e reseta o tempo de duracao 
       * para iniciar a contagem a partir da contagem atual. a proxima 
       * ação é definida como a de terminar/desligar o relé.
       **/
      digitalWrite(pinRele, HIGH);
      timerDuracaoBomba.Reset();
      proximaAcao(TERMINAR);
    }

    void desligar() {
      /**
       * Desliga o relé e reseta o tempo de espera para ligar novamente, 
       * a proxima ação é definida como a de esperar ficar pronta novamente.
       **/
      digitalWrite(pinRele, LOW);
      timerEsperaBomba.Reset();
      _acionarBomba = false;
      proximaAcao(ESPERAR);
    }

    boolean IsDesligar() {
      // Verifica a necessidade de desligar a bomba.
      return ((timerDuracaoBomba.IsEncerrado()) && (acaoBomba == TERMINAR));
    }

    boolean IsLigar() {
      // Verifica a necessidade de ligar a bomba.
      return (_acionarBomba && (acaoBomba == PRONTO));
    }

    boolean IsEsperar() {
      // Verifica a necessidade de esperar para ligar novamente.
      return (!(timerEsperaBomba.IsEncerrado()) && (acaoBomba != PRONTO ));
    }

};

/*********************************************
   Configuracao da esp:
   Pino 10 RX Esp_Serial(RX, TX) -> Porta TX do Esp-01
   Pino 11 TX Esp_Serial(RX, TX) -> Porta RX do Esp-01
**********************************************/
SoftwareSerial ESP_Serial(pinRX, pinTX);

/*
 * Defindo o sensor dht que será usado
 **/
DHT dht(pinDHT, DHT11);


BombaDAgua bomba(pinReleBomba);
Cronometro timerPutJsonData(90);


void setup() {
  /**
    * Inicia a configuracao dos pinos dos sensores e do esp
    **/
  if (DEBUG_REDE || DEBUG_SENSORES) {
    Serial.begin(9600);
  }
  ESP_Serial.begin(9600);

  pinMode(pinUmidSolo, INPUT);
  pinMode(pinLedVermelho, OUTPUT);
  pinMode(pinLedVerde, OUTPUT);

  bomba.init();
  dht.begin();
  
  connectWifi(); // conecta na rede wifi
}


void loop() {
  delay(1000);

  verificarSensores(); //Atualiza os dados do sensores

  /**
   * Só inicia o processo da requisição quando o tempo de espera tiver encerrado e quando a 
   * bomba d'agua não estiver ligada, visando evitar da bomba ficar mais tempo acionada do que 
   * deveria por causa do tempo que a requisição leva pra terminar.
   */
  if (timerPutJsonData.IsEncerrado() && (bomba.IsAcao(ESPERAR) || bomba.IsAcao(PRONTO))) {
    Serial.println("Iniciando Requests...");
    
    connectWifi(); // conecta na rede wifi caso a conexão seja encerrada por erro.
    
    int sendProx = 0;
    int foi = false;

    while (!foi) {

      delay(6000);

      connectHost();

      if (!sendProx) {
        PutDHT11();
        sendProx = 1;
      }
      else {
        PutSoil();
        sendProx = 0;
        foi = true;
      }

      asError();

      String closeCommand = "AT+CIPCLOSE";
      sendCommand(closeCommand, 6000, DEBUG_REDE);
    }
    timerPutJsonData.Reset();
  }
  // else {
  //   if (DEBUG_REDE) {
  //     Serial.println(F("!!!!---Aguardando a rede ficar pronta..."));
  //   }
  // }
  delay(2000);
}

void connectWifi() {
/**
  * Prepara para conecar na rede wifi.
  **/
  while (!asConn) {
    sendCommand("AT+RST", 2000, DEBUG_REDE);
    sendCommand("AT+CWMODE=1", 2000, DEBUG_REDE);

    Serial.println("Conectando a rede...");
    String CWJAP = "AT+CWJAP=\"";
    CWJAP += NOME_WIFI;
    CWJAP += "\",\"";
    CWJAP += PASSWORD;
    CWJAP += "\"";
    sendCommand(CWJAP, 10000, DEBUG_REDE);

    delay(2000);

    if (resposta.indexOf("OK") == -1) {
      Serial.println(F("Atencao: Nao foi possivel conectar a rede WiFi."));
    }
    else {
      Serial.print(F("Sucesso! Conecado a Rede WiFi "));
      Serial.println(NOME_WIFI);

      Serial.println(F("Obtendo endereco de IP na rede..."));
      sendCommand("AT+CIFSR", 1000, DEBUG_REDE);

      Serial.println(F("Configurando para multiplas conexoes..."));
      sendCommand("AT+CIPMUX=1", 1000, DEBUG_REDE);

      asConn = true;

      dualLedColor(LED_GREEN);
    }
  }
}


void dualLedColor(int cor[]) {
 /**
  * Define as cores do led ky-019
  **/
  analogWrite(pinLedVermelho, cor[0]);
  analogWrite(pinLedVerde, cor[1]);
}

boolean asError() {
 /**
  * Verifica se teve algum erro.
  **/
  if (resposta.indexOf("Error") != -1 || resposta.indexOf("busy p") != -1) {
    asConn = false;
    return true;
  }
  return false;
}

boolean connectHost() {
 /**
  * Prepara para iniciar a conexão TCP com o host, para enviar os dados.
  **/
  String cmd = "AT+CIPSTART=1,\"TCP\",\"";
  cmd += HOST;
  cmd += "\",";
  cmd += PORT;

  sendCommand(cmd, 3000, DEBUG_REDE);

  if (ESP_Serial.find("Error")) {
    Serial.println(F("\n-----> Ocorreu um Erro ao tentar connectar ao Host!"));
    return false;
  }

  return true;
}

void PutDHT11() {
/**
  * Put Request com o corpo JSON para enviar os dados do sensor DHT11, 
  * após preparar, é enviado o conteudo e então a conexão é encerrada
  **/
  String body = String("{\"nome\":\"Sensor de Temperatura e Umidade (DHT11)\",\"status\":[\"Temperatura: ");
  body += tempAr;
  body += "ºC\",\"Umidade: ";
  body += umidAr;
  body += "%\"]}";

  int content_length = body.length();

  String request = String("PUT /api/sensores/1 HTTP/1.1\r\n");
  request += "Host: ";
  request += HOST;
  request += "\r\n";
  request += "Connection: close\r\n";
  request += "Content-Type: application/json\r\n";
  request += "Content-Length: ";
  request += content_length;
  request += "\r\n";
  request += "\r\n";
  request += body;
  request += "\r\n";
  request += "\r\n";

  String cipSend = "AT+CIPSEND=1,";
  cipSend += request.length();

  sendCommand(cipSend, 3000, DEBUG_REDE);
  sendCommand(request, 10000, DEBUG_REDE);


  String closeCommand = "AT+CIPCLOSE";
  sendCommand(closeCommand, 10000, DEBUG_REDE);
}

void PutSoil() {
 /**
  * Put Request com o corpo JSON para enviar os dados do sensor de umidade do solo, 
  * após preparar, é enviado o conteudo e então a conexão é encerrada
  **/
  String body = String("{\"nome\":\"Sensor de Umidade do Solo \",\"status\":[\"Umidade em: ");
  body += porcentUmidSolo;
  body += "%\"]}";

  int content_length = body.length();

  String request = String("PUT /api/sensores/2 HTTP/1.1\r\n");
  request += "Host: ";
  request += HOST;
  request += "\r\n";
  request += "Connection: close\r\n";
  request += "Content-Type: application/json\r\n";
  request += "Content-Length: ";
  request += content_length;
  request += "\r\n";
  request += "\r\n";
  request += body;
  request += "\r\n";
  request += "\r\n";

  String cipSend = "AT+CIPSEND=1,";
  cipSend += request.length();

  sendCommand(cipSend, 3000, DEBUG_REDE);
  sendCommand(request, 10000, DEBUG_REDE);


  String closeCommand = "AT+CIPCLOSE";
  sendCommand(closeCommand, 10000, DEBUG_REDE);
}

String sendCommand(String command, unsigned int timeout, boolean debug)
{
 /**
  * Envia os comandos para o esp8266 via AT comands
  **/
  resposta = "";
  ESP_Serial.println(command);
  unsigned long timeIn = millis();
  while ( (timeIn + timeout) > millis())
  {
    if (ESP_Serial.available())
    {
      char c = ESP_Serial.read();
      resposta += c;
    }
  }
  if (debug)
  {
    Serial.println(resposta);
  }
  return resposta;
}

void verificarSensores() {
 /**
  * Atualiza as variaveis globais com os dados atuais coletados dos sensores,  
  * bem como verificar a necessidade de ligar a bomba.
  **/
  
  delay(100);
  valor_analogico = analogRead(pinUmidSolo);

  porcentUmidSolo = 100 - (valor_analogico / 10.24);

  delay(100);

  if (porcentUmidSolo >= 65 && porcentUmidSolo < 100)
  {
    verificarStatusSolo(valor_analogico, LED_GREEN, "-> Status: Solo umido", DEBUG_SENSORES);
  }
  if (porcentUmidSolo >= 40 && porcentUmidSolo < 65)
  {
    verificarStatusSolo(valor_analogico, LED_ORANGE, "-> Status: Umidade moderada", DEBUG_SENSORES);
    if (porcentUmidSolo < 60)
    {
      bomba.prepararLigarBomba(5, 1800); // Prepara para Ligar a bomba d'agua por 5 segundos e espera por 30 minutos
    }
  }
  if (porcentUmidSolo >= 0 && porcentUmidSolo < 40)
  {
    verificarStatusSolo(valor_analogico, LED_RED, "-> Status: Solo seco", DEBUG_SENSORES);
    bomba.prepararLigarBomba(10, 300); // Ligar a bomba d'agua por 10 segundos e espera 5 minutos para ligar novamente.
  }

  verificarDHT11(DEBUG_SENSORES);
  verificarBomba(DEBUG_SENSORES);
}

void verificarStatusSolo(int valor_umidade, int cor[], String status_msg, boolean debug) {
  if (bomba.IsAcao(TERMINAR)) {
    return;
  }
  dualLedColor(cor);
  if (debug) {
    Serial.print("MONITORAMENTO -- SOLO --");
    Serial.print(status_msg);
    Serial.print(" | -> Analógica: ");
    Serial.print(valor_umidade);
    Serial.print(" | -> Umidade em ");
    Serial.print(porcentUmidSolo);
    Serial.println("%");
  }
  delay(100);
}

void verificarDHT11(boolean debug) {
  if (bomba.IsAcao(TERMINAR)) {
    return;
  }
  
  /** 
   * A leitura da temperatura ou umidade pode levar aprximadamente 300ms
   * O atraso do sensor pode chegar a 2 segundos
   **/
  tempAr = dht.readTemperature(); // lê a temperatura em Graus Celsius
  umidAr = dht.readHumidity(); // lê a umidade em %

  if (isnan(umidAr) || isnan(tempAr)) {
    if (debug) {
      Serial.println("Falha na leitura do Sensor DHT!");
    }
    umidAr = 0.0;
    tempAr = 0.0;
  }
  else {
    if (debug) {
      Serial.print("---->>>");
      Serial.print("Temperatura do Ar: ");
      Serial.print(tempAr);
      Serial.print(" *C ");

      Serial.print("|| "); // tabulação

      Serial.print("Umidade: ");
      Serial.print(umidAr);
      Serial.print(" %\t");

      Serial.println(); // nova linha
    }
  }
}


void verificarBomba(boolean debug) {
/**
 * Verifica os estados da bomba d'agua para verificar se é pra liga-la, 
 * desliga-la ou se é pra esperar para ligar novamente.
 **/
  if (bomba.IsLigar()) {
    if (debug) {
      Serial.println("---->> Bomba Ligada!");
    }
    dualLedColor(LED_OFF);
    bomba.ligar();
    return;
  }

  if (bomba.IsDesligar()) {
    if (debug) {
      Serial.println("---->> Deligar bomba!");
    }
    bomba.desligar();
    return;
  }

  if (bomba.IsEsperar()) {

    if (debug) {
      Serial.println(F("---->> Esperando para ligar novamente!"));
    }
    return;
  }

  bomba.proximaAcao(PRONTO);
  if (debug) {
    Serial.println(F("---->> Pronta para acionar a bomba d'agua novamente...!"));
  }
}
