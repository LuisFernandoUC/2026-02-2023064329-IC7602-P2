import com.sun.net.httpserver.HttpExchange;
import com.sun.net.httpserver.HttpServer;

import java.io.IOException;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.nio.charset.StandardCharsets;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Collectors;

public class Main {
    private static final Map<Integer, Item> items = new ConcurrentHashMap<>();
    private static final AtomicInteger nextId = new AtomicInteger(1);

    private record Item(int id, String name, String description) {
        String toJson() {
            return "{\"id\":" + id + ",\"name\":\"" + escape(name) + "\",\"description\":\"" + escape(description) + "\"}";
        }
    }

    public static void main(String[] args) throws IOException {
        int port = Integer.parseInt(System.getenv().getOrDefault("PORT", "8080"));
        HttpServer server = HttpServer.create(new InetSocketAddress(port), 0);
        server.createContext("/health", ex -> send(ex, 200, "{\"status\":\"ok\"}"));
        server.createContext("/api/hello", ex -> send(ex, 200, "{\"message\":\"Rest API Java del Proyecto 2\"}"));
        server.createContext("/api/items", Main::handleItems);
        server.start();
        System.out.println("Rest API Java escuchando en el puerto " + port);
    }

    private static void handleItems(HttpExchange ex) throws IOException {
        try {
            String[] parts = ex.getRequestURI().getPath().split("/");
            // parts: ["", "api", "items", "<id>"?]
            Integer id = parts.length > 3 ? parseId(parts[3]) : null;
            if (parts.length > 3 && id == null) {
                send(ex, 400, error("El id debe ser un número"));
                return;
            }
            switch (ex.getRequestMethod()) {
                case "GET" -> {
                    if (id == null) {
                        String list = items.values().stream()
                                .sorted((a, b) -> Integer.compare(a.id(), b.id()))
                                .map(Item::toJson)
                                .collect(Collectors.joining(",", "[", "]"));
                        send(ex, 200, list);
                    } else {
                        Item item = items.get(id);
                        send(ex, item == null ? 404 : 200, item == null ? error("No existe") : item.toJson());
                    }
                }
                case "POST" -> {
                    if (id != null) {
                        send(ex, 405, error("Use POST sobre /api/items"));
                        return;
                    }
                    String body = readBody(ex);
                    String name = field(body, "name");
                    if (name == null || name.isBlank()) {
                        send(ex, 400, error("El campo name es obligatorio"));
                        return;
                    }
                    String description = field(body, "description");
                    Item item = new Item(nextId.getAndIncrement(), name, description == null ? "" : description);
                    items.put(item.id(), item);
                    send(ex, 201, item.toJson());
                }
                case "PUT" -> {
                    if (id == null || !items.containsKey(id)) {
                        send(ex, 404, error("No existe"));
                        return;
                    }
                    String body = readBody(ex);
                    String name = field(body, "name");
                    if (name == null || name.isBlank()) {
                        send(ex, 400, error("El campo name es obligatorio"));
                        return;
                    }
                    String description = field(body, "description");
                    Item item = new Item(id, name, description == null ? "" : description);
                    items.put(id, item);
                    send(ex, 200, item.toJson());
                }
                case "DELETE" -> {
                    if (id == null || items.remove(id) == null) {
                        send(ex, 404, error("No existe"));
                        return;
                    }
                    ex.sendResponseHeaders(204, -1);
                    ex.close();
                }
                default -> send(ex, 405, error("Método no permitido"));
            }
        } catch (RuntimeException e) {
            send(ex, 500, error("Error interno"));
        }
    }

    private static Integer parseId(String text) {
        try {
            return Integer.valueOf(text);
        } catch (NumberFormatException e) {
            return null;
        }
    }

    private static String readBody(HttpExchange ex) throws IOException {
        return new String(ex.getRequestBody().readAllBytes(), StandardCharsets.UTF_8);
    }

    /** Lee un campo de texto de un JSON plano, por ejemplo {"name":"x"}. */
    private static String field(String json, String key) {
        Matcher m = Pattern.compile("\"" + key + "\"\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\"").matcher(json);
        return m.find() ? m.group(1).replace("\\\"", "\"").replace("\\\\", "\\") : null;
    }

    private static String escape(String text) {
        return text.replace("\\", "\\\\").replace("\"", "\\\"");
    }

    private static String error(String message) {
        return "{\"error\":\"" + escape(message) + "\"}";
    }

    private static void send(HttpExchange ex, int status, String body) throws IOException {
        byte[] bytes = body.getBytes(StandardCharsets.UTF_8);
        ex.getResponseHeaders().set("Content-Type", "application/json; charset=utf-8");
        ex.sendResponseHeaders(status, bytes.length);
        try (OutputStream os = ex.getResponseBody()) {
            os.write(bytes);
        }
    }
}
