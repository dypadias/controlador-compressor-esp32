#  EletroMatos - Controlador Inteligente de Compressor de Poço (v5.0)

Este projeto transforma um ESP32 em um controlador robusto e inteligente para um compressor de poço, com monitoramento e controle via interface web.

<!-- ![Interface do Projeto](https://i.imgur.com/YOUR_SCREENSHOT_URL.png)  -->
---

## ✨ Funcionalidades Principais

* **Interface Web Completa:** Monitore status, temperatura e controle o compressor de qualquer dispositivo na rede (celular ou computador).
* **Acesso Simplificado:** Acesse o painel facilmente pelo endereço amigável `http://compressor.local`.
* **Modo Automático Inteligente:** Controle de ciclo liga/desliga baseado em temporizadores configuráveis.
* **Proteção do Equipamento:** Desligamento automático por superaquecimento (com temperatura máxima ajustável) e por caixa d'água cheia.
* **Métricas de Desempenho:** Registra o histórico dos últimos 5 enchimentos, incluindo o tempo total do ciclo e a quantidade de acionamentos do compressor.
* **Gráfico de Temperatura:** Visualização do histórico de temperatura das últimas 24 horas.
* **Memória Persistente:** Salva todas as configurações e contadores, que não são perdidos em caso de queda de energia.

## 🛠️ Hardware Necessário

* Placa de desenvolvimento baseada no ESP32.
* Módulo Relé de 1 canal para acionar o compressor.
* Sensor de Temperatura DS18B20 (à prova d'água).
* Resistor de 4.7kΩ (pull-up para o DS18B20).
* Sensor de nível de água (boia) para detectar caixa cheia.
* Fonte de alimentação para o ESP32.

## ⚙️ Configuração e Instalação

Este projeto foi desenvolvido utilizando **Visual Studio Code** com a extensão **PlatformIO**.

1.  **Clone o repositório:**
    ```bash
    git clone https://github.com/dypadias/controlador-compressor-esp32
    ```
2.  **Abra o projeto no VS Code:**
    * Vá em `File > Open Folder` e selecione a pasta do projeto.
    * O PlatformIO irá detectar o arquivo `platformio.ini` e instalará automaticamente todas as dependências (`OneWire`, `DallasTemperature`, etc.).
3.  **Envie o código e os arquivos:**
    * Na barra de status do PlatformIO, clique na **seta (➡️)** para compilar e enviar o firmware (`main.cpp`).
    * No menu lateral do PlatformIO, vá em `Project Tasks > env:esp32dev > Platform > Upload Filesystem Image` para enviar a interface web (`index.html`).
4.  **Configure o Wi-Fi:**
    * Após a primeira inicialização, o ESP32 criará uma rede Wi-Fi chamada `EletroMatos_Compressor`.
    * Conecte-se a ela (senha: `12345678`) e acesse `192.168.4.1` para configurar a conexão com a sua rede local.
