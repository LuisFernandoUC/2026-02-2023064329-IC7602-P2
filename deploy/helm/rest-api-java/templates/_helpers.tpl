{{- define "rest-api-java.name" -}}
{{- .Chart.Name -}}
{{- end -}}

{{- define "rest-api-java.fullname" -}}
{{- printf "%s-%s" .Release.Name .Chart.Name | trunc 63 | trimSuffix "-" -}}
{{- end -}}

{{- define "rest-api-java.labels" -}}
app.kubernetes.io/name: {{ include "rest-api-java.name" . }}
app.kubernetes.io/instance: {{ .Release.Name }}
app.kubernetes.io/managed-by: {{ .Release.Service }}
helm.sh/chart: {{ printf "%s-%s" .Chart.Name .Chart.Version }}
{{- end -}}
