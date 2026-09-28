# Infraestructura AWS

Este documento describe la infraestructura de AWS sobre la que corre el controlador. El proyecto no utiliza herramientas de infraestructura como código (Terraform, CloudFormation); este documento cumple esa función de forma textual, con los valores y nombres reales utilizados.

## 1. VPC

| Parametro | Valor |
|---|---|
| Nombre | autoscaling-vpc |
| CIDR | 172.16.0.0/16 |
| DNS hostnames | Habilitado |
| DNS resolution | Habilitado |
| IPv6 | No asignado |

## 2. Subredes

Se crean 4 subredes, distribuidas en 2 zonas de disponibilidad.

| Subred | Tipo | Zona de disponibilidad | CIDR |
|---|---|---|---|
| Publica 1a | Publica | us-east-1a | 172.16.1.0/24 |
| Publica 1b | Publica | us-east-1b | 172.16.4.0/24 |
| Privada 1a | Privada | us-east-1a (use1-az6) | 172.16.2.0/24 |
| Privada 1b | Privada | us-east-1b (use1-az1) | 172.16.5.0/24 |

Las subredes publicas tienen ruta hacia un Internet Gateway. Las subredes privadas no tienen NAT Gateway: las instancias de la aplicacion no requieren salida a internet porque arrancan desde una AMI con todo el software preinstalado, sin necesidad de descargar paquetes en el arranque.

## 3. Internet Gateway

Un Internet Gateway asociado a la VPC, con ruta desde ambas subredes publicas.

## 4. Security Groups

| Security Group | Proposito | Reglas de entrada |
|---|---|---|
| SG-Bastion | Bastion host | SSH (22) desde IP del administrador |
| SG-ALB | Load Balancer | HTTP (80) desde Internet (o desde la IP del generador de carga) |
| SG-App | Instancias de la aplicacion | HTTP (80) desde SG-ALB; SSH (22) desde SG-Bastion |
| SG-Controller | EC2 del controlador | SSH (22) desde IP del administrador |

Las reglas de salida se dejan en su valor por defecto (todo el trafico permitido) en los cuatro grupos.

## 5. Bastion Host

| Parametro | Valor |
|---|---|
| Ubicacion | Subred publica 1a |
| Tipo de instancia | t2.micro |
| AMI | Ubuntu Server 24.04 LTS |
| IP publica | Si (asignada al lanzar) |
| Security Group | SG-Bastion |

## 6. AMI de la aplicacion

Se construye una AMI propia a partir de una instancia temporal:

1. Lanzar una instancia t2.micro en subred publica, Ubuntu 24.04.
2. Instalar Flask y copiar la aplicacion (infra/app/app.py) con dos rutas: `/` (endpoint de carga de CPU) y `/health` (verificacion de salud).
3. Instalar el servicio systemd (infra/app/app.service) con reinicio automatico.
4. Verificar que la aplicacion responde y que sobrevive a un reinicio de la instancia.
5. Crear la imagen (EC2 -> Actions -> Create image).
6. Terminar la instancia temporal una vez creada la AMI.

## 7. Target Group

| Parametro | Valor |
|---|---|
| Nombre | TG-App |
| Protocolo / Puerto | HTTP / 80 |
| Tipo de destino | Instances |
| VPC | autoscaling-vpc |
| Health check path | /health |
| Intervalo de health check | 5 segundos |
| Umbral saludable | 2 verificaciones exitosas |
| Umbral no saludable | 2 verificaciones fallidas |
| Timeout de health check | Menor al intervalo configurado |
| Deregistration delay | 30 segundos |

## 8. Application Load Balancer

| Parametro | Valor |
|---|---|
| Nombre | ALB-App |
| Tipo | Application Load Balancer, internet-facing |
| Subredes | Publica 1a y Publica 1b (ambas obligatorias) |
| Security Group | SG-ALB |
| Listener | HTTP:80 -> forward a TG-App |

## 9. Launch Template

| Parametro | Valor |
|---|---|
| Nombre | LT-App |
| AMI | La construida en el paso 6 |
| Tipo de instancia | t2.micro |
| Security Group | SG-App |
| Subred | No especificada en la plantilla (la define el ASG) |
| IP publica | Deshabilitada |
| Monitoreo detallado | Habilitado (metricas de CPU cada 1 minuto) |
| User data | Ninguno (la aplicacion ya esta en la AMI) |

## 10. Auto Scaling Group

| Parametro | Valor |
|---|---|
| Nombre | ASG-App |
| Launch Template | LT-App |
| Subredes | Privada 1a y Privada 1b |
| Target Group asociado | TG-App |
| Capacidad minima | 1 |
| Capacidad maxima | 5 |
| Capacidad deseada inicial | 1 |
| Health check | Basado en el Target Group (ELB) |
| Health check grace period | 90 segundos |
| Politicas de escalado dinamico | Ninguna (el escalado lo decide el controlador, no el ASG) |
| Metricas de grupo | Habilitadas |

## 11. EC2 del controlador

| Parametro | Valor |
|---|---|
| Ubicacion | Subred publica (1a o 1b) |
| Tipo de instancia | t2.micro |
| IP publica | Si |
| Security Group | SG-Controller |
| Rol / Instance profile | LabInstanceProfile (AWS Academy) o rol con la politica de docs/iam-policy-controller.json en una cuenta propia |

