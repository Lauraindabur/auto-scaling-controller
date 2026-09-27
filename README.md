# Auto-Scaling Controller en C++ para AWS

Controlador de autoescalado horizontal en C++ para ejecutarse como un proceso continuo en una única instancia EC2. Implementa un ciclo de decisión híbrido reactivo-proactivo: observa métricas de CloudWatch, combina una política basada en umbrales de CPU con un pronóstico de demanda mediante suavizado de Holt, y ajusta la capacidad del ASG en consecuencia

---

## Tabla de contenidos

1. [Requisitos previos](#requisitos-previos)
2. [Quick start: compilar y ejecutar](#quick-start-compilar-y-ejecutar)
3. [Estructura del repositorio](#estructura-del-repositorio)
4. [Configuración](#configuración)
5. [Cómo decide el controller](#cómo-decide-el-controller)
6. [Métricas de CloudWatch](#métricas-de-cloudwatch)
7. [Cómo leer el log de decisiones](#cómo-leer-el-log-de-decisiones)
8. [Documentación adicional](#documentación-adicional)
9. [Diagramas de arquitectura](#diagramas-de-arquitectura)

---

## Requisitos previos

### En la máquina (compilación)

- **CMake** ≥ 3.13
- **C++17** (g++ o clang)
- **vcpkg** (package manager de C++): [Instalar](https://github.com/Microsoft/vcpkg)
- **AWS SDK for C++** (se instala vía vcpkg): `monitoring` + `autoscaling`

### En AWS

- **VPC** con subredes públicas y privadas
- **Application Load Balancer (ALB)** con Target Group que apunta a las instancias del ASG
- **Auto Scaling Group (ASG)** con mín 1 y máx 5 instancias (configurables)
- **AMI personalizada** con app Flask (ver [Construcción de la AMI](#construcción-de-la-ami))
- **EC2 para el controller** con rol IAM que permita CloudWatch + Auto Scaling (ver `docs/iam-policy-controller.json`)
- **Región:** `us-east-1` (configurable en `controller.env`)

---

## Quick start: compilar y ejecutar

### 1. Clonar y navegar

```bash
git clone <repo-url>
cd controller
```

### 2. Instalar dependencias 

```bash
# Instalar vcpkg 
git clone https://github.com/Microsoft/vcpkg.git
./vcpkg/bootstrap-vcpkg.sh

# Instalar dependencias del proyecto
./vcpkg/vcpkg install aws-sdk-cpp[monitoring,autoscaling]:x64-linux
```

### 3. Compilar

```bash
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=../vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build . -j4
cd ..
```

### 4. Configurar variables de entorno

```bash
cp config/controller.env.example config/controller.env
# Edita config/controller.env con los valores reales de tu cuenta AWS:
#   - ASG_NAME
#   - REGION
#   - LOAD_BALANCER_DIM
#   - TARGET_GROUP_DIM
```

### 5. Ejecutar

```bash
set -a; source config/controller.env; set +a
./build/controller
```

El controller entra en un bucle infinito: cada 30 segundos consulta CloudWatch, y cada 60 segundos toma una decisión. Escribe sus logs en `logs/decisions.jsonl`.

---


## Configuración

Todas las variables están en `config/controller.env.example` (defaults) y se cargan de `config/controller.env` (que NO se versiona).

### Variables obligatorias (sin default)

| Variable | Ejemplo | Significado |
|---|---|---|
| `ASG_NAME` | `ASG-App` | Nombre del Auto Scaling Group |
| `REGION` | `us-east-1` | Región AWS |
| `LOAD_BALANCER_DIM` | `app/MI-ALB/abc123def456` | Dimensión del ALB en CloudWatch |
| `TARGET_GROUP_DIM` | `targetgroup/MI-TG/xyz789abc123` | Dimensión del Target Group en CloudWatch |

### Variables de decisión 

| Variable | Default | Rango | Significado |
|---|---|---|---|
| `UMBRAL_ALTO` | 70.0 | 0-100 | CPU (%) → subir |
| `UMBRAL_BAJO` | 30.0 | 0-100 | CPU (%) → bajar |
| `MARGEN_BAJADA` | 10.0 | 0-100 | Holgura del chequeo n-1 |
| `MA_VENTANA` | 3 | ≥1 | Muestras para media móvil de CPU |
| `HOLT_ALPHA` | 0.5 | 0-1 | Respuesta rápida del nivel en Holt |
| `HOLT_BETA` | 0.3 | 0-1 | Respuesta lenta de la tendencia en Holt |
| `HORIZONTE_PERIODOS` | 3 | ≥1 | Minutos adelante que predice Holt |
| `C_RPM` | 300.0 | >0 | Capacidad de 1 instancia (req/min) |
| `MIN_CAPACITY` | 1 | ≥1 | Mínimo de instancias |
| `MAX_CAPACITY` | 5 | ≥1 | Máximo de instancias |

---

## Cómo decide el controller

### Decision Engine

El **Decision Engine** orquesta un ciclo completo cada 60 segundos:

1. **Leer métricas** de CloudWatch (CPU, RequestCount, HealthyHostCount)
2. **Evaluar calidad** (¿datos completos y frescos?)
3. **Calcular dos señales independientes** (reactiva y proactiva)
4. **Combinar señales** (OR para subir, AND para bajar)
5. **Aplicar guardas de seguridad** (cooldowns, límites, chequeo n-1)
6. **Ejecutar SetDesiredCapacity** si corresponde
7. **Registrar la decisión** en el log JSONL

### Señal Reactiva 

Responde a la **carga actual medida ahora**. Lee la CPU promedio del ASG de los últimos 3 minutos (media móvil de 3 muestras) y compara contra umbrales fijos: sube si MA_CPU > 70%, baja si MA_CPU < 30%, mantiene en cualquier otro caso. Es simple, rápida y defensiva: reacciona a picos reales. La media móvil filtra ruido de 1 minuto.

### Señal Proactiva 

Anticipa la **carga futura esperada**. Usa Holt (double exponential smoothing) para aprender la tendencia del RequestCount total y proyecta la demanda 3 minutos adelante. Traduce ese pronóstico a instancias necesarias (pronóstico / 300 RPM por instancia). Emite UP si la demanda crecerá, pero solo si ese aumento es sostenido (últimos 3 minutos en tendencia creciente: evita picos aislados). Emite DOWN si detecta descenso sostenido de demanda.

---

## Métricas de CloudWatch

| Métrica | Namespace | Dimensión | Stat | Período | Uso |
|---|---|---|---|---|---|
| `CPUUtilization` | `AWS/EC2` | `AutoScalingGroupName` | Average | 60s | Señal reactiva |
| `RequestCount` | `AWS/ApplicationELB` | `LoadBalancer` | Sum | 60s | Señal proactiva (Holt) |
| `HealthyHostCount` | `AWS/ApplicationELB` | `LoadBalancer` + `TargetGroup` | Average | 60s | Confirmación de capacidad real |

---


## Documentación adicional

### Dentro del repo

- **`infra/app/app.py`**: App Flask que corre en las instancias del ASG (genera carga y health endpoint)
- **`infra/app/app.service`**: Systemd para la app
- **`infra/controller.service`**: Systemd para el controller
- **`docs/iam-policy-controller.json`**: Política IAM recomendada (reemplazar REGION/ACCOUNT_ID)
- **`loadtest/scenarios/escenario_completo.csv`**: Perfil de carga: 7 fases (base, pico, recuperación, rampa, meseta, bajada, reposo)

---

## Diagramas de arquitectura

### Flujo del ciclo de control

```mermaid
flowchart TB
    A["1. Leer métricas<br>CPU, RequestCount, HealthyHostCount"] --> B["2. Evaluar calidad<br>¿Son datos completos y frescos?"]
    B --> C["3. Calcular dos señales<br>Reactiva + Proactiva"]
    C --> D["4. Combinar señales<br>+ revisar guardas de seguridad"]
    D --> E["5. Actuar y registrar<br>SetDesiredCapacity + log"]
    E -. 60 segundos después .-> A

    style A fill:#e7f1ff,stroke:#4a90d9
    style B fill:#e7f1ff,stroke:#4a90d9
    style C fill:#e8e0ff,stroke:#6b4fd6
    style D fill:#e8e0ff,stroke:#6b4fd6
    style E fill:#d4edda,stroke:#28a745
```

### Señal Reactiva

```mermaid
flowchart TD
    R0["Cada 60 segundos<br/>CloudWatch publica CPU promedio del ASG"]
    R0 --> R1["Se guardan los últimos<br/>3 valores de CPU"]
    R1 --> R2{"¿Ya hay 3 valores<br/>en el historial?"}

    R2 -->|No| RHOLD0["⏸ HOLD<br/>No cambiar capacidad<br/>Todavía no hay suficiente historia"]
    R2 -->|Sí| R3["Calcular MA3 = promedio<br/>de los últimos 3 minutos"]

    R3 --> R4{"¿MA3 > UMBRAL_ALTO<br/>70%?"}
    R4 -->|Sí| RUP[" UP<br/>La CPU está muy alta<br/>Pedir más instancias"]
    R4 -->|No| R5{"¿MA3 < UMBRAL_BAJO<br/>30%?"}

    R5 -->|Sí| RDOWN[" DOWN<br/>La CPU está muy baja<br/>Se puede quitar instancias"]
    R5 -->|No| RHOLD["HOLD<br/>La CPU está en zona segura<br/>Mantener capacidad"]

    classDef metric fill:#e7f1ff,stroke:#4a90d9,stroke-width:2px
    classDef calc fill:#eee8ff,stroke:#6b4fd6,stroke-width:1px
    classDef up fill:#e8f5e9,stroke:#4caf50,stroke-width:2px
    classDef down fill:#ffebee,stroke:#e53935,stroke-width:2px
    classDef hold fill:#f5f5f5,stroke:#888,stroke-width:1px

    class R0 metric
    class R1,R3 calc
    class RUP up
    class RDOWN down
    class RHOLD,RHOLD0 hold
```

### Señal Proactiva (Holt)

```mermaid
flowchart TB
    P0["Cada 60 segundos<br>CloudWatch publica RequestCount total del ALB"] --> P1["Actualizar modelo Holt:<br>Nivel (demanda actual suavizada)<br>+ Tendencia (velocidad de cambio)"]
    P1 --> P2{"¿Hay suficiente<br>historia?<br>2+ datos"}
    P2 -- No --> PHOLD0[" HOLD<br>No cambiar capacidad<br>Todavía no hay suficiente historia"]
    P2 -- Sí --> P3["Proyectar la demanda<br>de los próximos 3 minutos<br>usando Nivel + 3×Tendencia"]
    P3 --> P4["Calcular cuántas instancias<br>se necesitarían:<br>instancias = techo(pronóstico / 300 RPM)"]
    P4 --> P5{"¿Necesarias &gt; actuales<br>(InService+Pending)?"}
    P5 -- Sí --> P6{"¿La demanda ha subido<br>de forma sostenida<br>en los últimos 3 minutos?"}
    P6 -- Sí --> PUP["UP<br>Anticipar más capacidad<br>para la demanda esperada"]
    P6 -- No --> PHOLD1[" HOLD<br>Ignorar pico aislado<br>Esperar a confirmar tendencia"]
    P5 -- No --> P7{"¿Necesarias &lt; actuales?"}
    P7 -- Sí --> PDOWN["DOWN<br>Se proyecta capacidad sobrante<br>en los próximos minutos"]
    P7 -- No --> PHOLD2[" HOLD<br>La demanda proyectada<br>coincide con capacidad"]

     P0:::metric
     P1:::calc
     PHOLD0:::hold
     P3:::calc
     P4:::calc
     PUP:::up
     PHOLD1:::hold
     PDOWN:::down
     PHOLD2:::hold
    classDef metric fill:#e7f1ff,stroke:#4a90d9,stroke-width:2px
    classDef calc fill:#eee8ff,stroke:#6b4fd6,stroke-width:1px
    classDef up fill:#e8f5e9,stroke:#4caf50,stroke-width:2px
    classDef down fill:#ffebee,stroke:#e53935,stroke-width:2px
    classDef hold fill:#f5f5f5,stroke:#888,stroke-width:1px
```

### Combinación de señales y safetyguards

```mermaid
flowchart TB
    START["Señal Reactiva: UP/DOWN/HOLD<br><br>Señal Proactiva: UP/DOWN/HOLD"] --> Q1{"¿Alguna de las dos<br>dice SUBIR?"}
    Q1 -- No --> Q2{"¿Las DOS dicen<br>BAJAR?"}
    Q1 -- Sí --> G1["Evaluar safetygurads:<br>• Dato completo • No estamos en máximo (5)<br>• ≥120 seg desde última subida<br>• Sin instancias Pending bloqueadas"]
    G1 --> G1R{"¿Todas<br>pasan?"}
    G1R -- Sí --> UP["SUBE 1 instancia"]
    G1R -- No --> WAIT1["`**HOLD** Se mantiene igual<br>Motivo guardado en log`"]
    Q2 -- Sí --> G2["Evaluar safetyguards de bajada:<br>• Dato completo y frescos<br>• No estamos en mínimo (1)<br>• n confirmado por HealthyHostCount<br>• ≥240 seg desde última acción<br>• CPU proyectada &lt; 60%<br>• Demanda/instancia &lt; 300 RPM"]
    G2 --> G2R{"¿Todas<br>pasan?"}
    G2R -- Sí --> DOWN["`**BAJA** 1 instancia`"]
    G2R -- No --> WAIT2["`**HOLD** Se mantiene igual<br>Motivo guardado en log`"]
    Q2 -- No --> WAIT3["`**HOLD** Se mantiene igual<br>Señales no de acuerdo`"]

    style START fill:#e7f1ff,stroke:#4a90d9,stroke-width:2px
    style G1 fill:#e8f5e9,stroke:#4caf50
    style UP fill:#d4edda,stroke:#28a745,stroke-width:2px
    style WAIT1 fill:#fff3cd,stroke:#ffc107,stroke-width:1px
    style G2 fill:#ffebee,stroke:#e53935
    style DOWN fill:#d4edda,stroke:#28a745,stroke-width:2px
    style WAIT2 fill:#fff3cd,stroke:#ffc107,stroke-width:1px
    style WAIT3 fill:#fff3cd,stroke:#ffc107,stroke-width:1px
```

---

## Construcción de la AMI

La app de las instancias del ASG se crea una sola vez en una **AMI propia** (`ami-autoscaling-app-v1`), evitando que cada instancia tenga que instalar dependencias en el arranque.

### Procedimiento

1. Lanzar instancia temporal: Ubuntu 24.04 LTS, t2.micro, en una subred pública
2. Conectar por SSH y ejecutar:
   ```bash
   sudo apt update && sudo apt install -y python3-flask
   ```
3. Copiar archivos:
   ```bash
   sudo mkdir -p /opt/app
   sudo cp infra/app/app.py /opt/app/app.py
   sudo cp infra/app/app.service /etc/systemd/system/app.service
   ```
4. Habilitar y arrancar:
   ```bash
   sudo systemctl daemon-reload
   sudo systemctl enable --now app
   ```
5. Verificar:
   ```bash
   curl http://localhost/health    # → "ok"
   curl http://localhost/          # → "ok {número}" (genera CPU)
   ```
6. Prueba de persistencia (reiniciar sin volver a entrar por SSH):
   ```bash
   sudo reboot
   # Desde fuera: curl http://<ip-publica>/health → confirma que arranca solo
   ```
7. Limpiar y crear imagen:
   ```bash
   sudo apt clean
   ```

---
