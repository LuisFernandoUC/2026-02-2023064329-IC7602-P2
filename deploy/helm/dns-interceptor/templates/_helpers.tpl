{{- define "dns-interceptor.name" -}}
{{- .Chart.Name -}}
{{- end -}}

{{- define "dns-interceptor.fullname" -}}
{{- printf "%s-%s" .Release.Name .Chart.Name | trunc 63 | trimSuffix "-" -}}
{{- end -}}

{{- define "dns-interceptor.labels" -}}
app.kubernetes.io/name: {{ include "dns-interceptor.name" . }}
app.kubernetes.io/instance: {{ .Release.Name }}
app.kubernetes.io/managed-by: {{ .Release.Service }}
helm.sh/chart: {{ printf "%s-%s" .Chart.Name .Chart.Version }}
{{- end -}}

{{/*
IP de cada zona en formato zona=ip,zona=ip (valor de DI_ZONE_IPS).
Usa la IP indicada en values; si falta, la busca en el Service de la caché zonal desplegada.
*/}}
{{- define "dns-interceptor.zoneIPs" -}}
{{- $root := . -}}
{{- $pairs := list -}}
{{- range $name, $configured := .Values.zones -}}
  {{- $ip := $configured | default "" -}}
  {{- if not $ip -}}
    {{- $svcName := printf "%s-zonal-cache-%s" $root.Values.zonalCache.release $name -}}
    {{- $svc := lookup "v1" "Service" $root.Release.Namespace $svcName -}}
    {{- if $svc -}}{{- $ip = $svc.spec.clusterIP -}}{{- end -}}
  {{- end -}}
  {{- if $ip -}}{{- $pairs = append $pairs (printf "%s=%s" $name $ip) -}}{{- end -}}
{{- end -}}
{{- join "," $pairs -}}
{{- end -}}
