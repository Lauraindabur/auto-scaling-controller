# Deployment y ejecucion

Guia paso a paso para desplegar la infraestructura y poner a correr el controller.

## 1. Prerrequisitos

- Cuenta de AWS 
- AWS CLI instalado y configurado localmente (para exportar metricas y verificar recursos).
- Compilador con soporte de C++17 (probado con GCC 13, incluido en Ubuntu 24.04).
- CMake version 3.13 o superior.
- vcpkg (incluido como submodulo o carpeta en el repositorio; requiere ejecutar bootstrap-vcpkg.sh una vez).
- Un par de llaves SSH (key pair) para acceder a las instancias.

## 2. Construir la infraestructura

Seguir los pasos descritos en [infrastructure.md](./infrastructure.md), en este orden:

1. Crear la VPC con sus 4 subredes.
2. Crear los Security Groups.
3. Lanzar el Bastion Host.
4. Construir la AMI de la aplicacion.
5. Crear el Target Group.
6. Crear el Application Load Balancer.
7. Crear el Launch Template.
8. Crear el Auto Scaling Group.
9. Lanzar la EC2 del controlador, con el rol/instance profile correspondiente.

Al finalizar, verificar que el Target Group muestra al menos una instancia en estado healthy antes de continuar.

## 3. Compilar el controlador

Se compila en una maquina con suficiente memoria (no en la instancia t2.micro del controlador, que tiene 1 GB de RAM y puede no ser suficiente para compilar el SDK de AWS).

```
git clone <url-del-repositorio>
cd controller

./vcpkg/bootstrap-vcpkg.sh

cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=$PWD/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build
```

Al finalizar debe existir el binario en build/controller. La primera compilacion tarda mas porque vcpkg construye el SDK de AWS desde cero; las siguientes son mas rapidas porque el SDK queda cacheado.

## 4. Configurar

Copiar la plantilla de configuracion y completarla con los valores reales de la cuenta:

```
cp config/controller.env.example controller.env
```

Editar controller.env y completar, como minimo:

- ASG_NAME: nombre del Auto Scaling Group (ASG-App).
- LOAD_BALANCER_DIM: dimension del ALB para CloudWatch, formato app/<nombre>/<id>.
- TARGET_GROUP_DIM: dimension del Target Group para CloudWatch, formato targetgroup/<nombre>/<id>.
- REGION: region de AWS utilizada.

Los demas parametros (umbrales, cooldowns, C_RPM, horizonte de prediccion) tienen valores por defecto calibrados; ver la tabla completa en el README.

## 5. Copiar el binario a la EC2 del controlador

```
scp -i <llave>.pem build/controller controller.env infra/controller.service ubuntu@<IP-EC2-controlador>:~
```

## 6. Instalar el servicio en la EC2 del controlador

Conectarse por SSH:

```
ssh -i <llave>.pem ubuntu@<IP-EC2-controlador>
```

Dentro de la instancia:

```
sudo mv ~/controller.service /etc/systemd/system/controller.service
sudo systemctl daemon-reload
```

Revisar el archivo controller.service para confirmar las rutas del binario y del archivo de entorno antes de arrancar.


## 7. Arrancar el servicio

```
sudo systemctl enable --now controller
sudo systemctl status controller
```

## 8. Verificar que esta funcionando

```
tail -f logs/decisions.jsonl
```

Cada linea nueva corresponde a un ciclo de decision. Ver [logging.md](./logging.md) para el significado de cada campo.

## 10. Generar carga de prueba

Ver [load-testing.md](./load-testing.md) para el procedimiento de generacion de carga con k6.

## 11. Detener el controlador

```
sudo systemctl stop controller
```

El estado  queda guardado en disco y se recupera automaticamente en el siguiente arranque.