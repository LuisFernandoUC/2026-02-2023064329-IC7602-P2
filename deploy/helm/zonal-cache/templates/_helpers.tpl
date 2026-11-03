{{- define "zonal-cache.name" -}}
{{- .Chart.Name -}}
{{- end -}}

{{- define "zonal-cache.fullname" -}}
{{- printf "%s-%s" .Release.Name .Chart.Name | trunc 63 | trimSuffix "-" -}}
{{- end -}}

{{/* Nombre de los recursos de una zona: <release>-zonal-cache-<zona> */}}
{{- define "zonal-cache.zoneName" -}}
{{- printf "%s-%s" (include "zonal-cache.fullname" .root) .zone | trunc 63 | trimSuffix "-" -}}
{{- end -}}

{{- define "zonal-cache.labels" -}}
app.kubernetes.io/name: {{ include "zonal-cache.name" . }}
app.kubernetes.io/instance: {{ .Release.Name }}
app.kubernetes.io/managed-by: {{ .Release.Service }}
helm.sh/chart: {{ printf "%s-%s" .Chart.Name .Chart.Version }}
{{- end -}}

{{- define "zonal-cache.secretName" -}}
{{- default (printf "%s-secrets" (include "zonal-cache.fullname" .)) .Values.secrets.existingSecret -}}
{{- end -}}

{{- define "zonal-cache.tlsSecretName" -}}
{{- default (printf "%s-tls" (include "zonal-cache.fullname" .)) .Values.tls.existingSecret -}}
{{- end -}}
