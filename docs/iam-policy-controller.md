# Politica IAM del controller

Politica IAM de minimo privilegio para el controller. Define los unicos 3 permisos que
necesita: leer metricas de CloudWatch, leer el estado del Auto Scaling Group, y modificar
la capacidad solo del ASG-App (nunca crear/borrar instancias ni tocar otros
recursos). Corresponde a una cuenta de AWS propia; en AWS Academy se usa LabRole por
restriccion de la plataforma

El JSON de la politica esta en [iam-policy-controller.json](./iam-policy-controller.json).
Reemplazar `REGION` y `ACCOUNT_ID` antes de usarlo.

| Statement | Permiso | Resource | Motivo |
|---|---|---|---|
| `LeerMetricasDeCloudWatch` | `cloudwatch:GetMetricData` | `*` | GetMetricData no admite restriccion por recurso |
| `LeerEstadoDelAutoScalingGroup` | `autoscaling:DescribeAutoScalingGroups` | `*` | DescribeAutoScalingGroups no admite restriccion por recurso |
| `EscalarSoloElAsgDelController` | `autoscaling:SetDesiredCapacity` | ARN de ASG-App | Unico permiso de escritura; restringido al ASG especifico del controller |
