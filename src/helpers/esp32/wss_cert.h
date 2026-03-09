#ifndef WSS_CERT_H
#define WSS_CERT_H
// Self-signed cert for WSS (CN=meshcomod.local). First connection from HTTPS will show browser cert warning; accept once.

static const char WSS_SERVER_CERT_PEM[] =
  "-----BEGIN CERTIFICATE-----\n"
  "MIIDFTCCAf2gAwIBAgIUHr4MXKtINPXhRy5SmtNJUikIaxgwDQYJKoZIhvcNAQEL\n"
  "BQAwGjEYMBYGA1UEAwwPbWVzaGNvbW9kLmxvY2FsMB4XDTI2MDMwOTE3NDYxNloX\n"
  "DTM2MDMwNjE3NDYxNlowGjEYMBYGA1UEAwwPbWVzaGNvbW9kLmxvY2FsMIIBIjAN\n"
  "BgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAutKYguySA/Qc6VyyW55vhcke3/x6\n"
  "J1HBVryYcYPUpaMm7ju0Uwxkzz5KzFqyqxwSogSR3JE8Jsls+6utQUU6Qu/psZeM\n"
  "QeO3AN+ebi6xyoZ5riDuJDkljzMizDalyYS1ugnXPvhcxvPcY9/+qRpjQz6X37zx\n"
  "YLygxtWbjvepOO7YhfLBkVAsOX674a8VTA5RETtOk3Sdpt8MGfM5mhYhWT5CRj7m\n"
  "odXrhT856T2z58Zo9GNJN6wqUqTi/noeTH4VZN9fGRBgeynmNerThCcI4b3Mo3Oi\n"
  "F4vltI0d/wOJmTgm2BcgfdOjGtso3zjIsYUtFlGFDiP/VUOyAgVX6KA2JwIDAQAB\n"
  "o1MwUTAdBgNVHQ4EFgQU8JDbMqYT43n3Pbj4ohUTRgsHTHwwHwYDVR0jBBgwFoAU\n"
  "8JDbMqYT43n3Pbj4ohUTRgsHTHwwDwYDVR0TAQH/BAUwAwEB/zANBgkqhkiG9w0B\n"
  "AQsFAAOCAQEAnzpF1X1a75bDXpE+pGVWrA/3ZyFrpY2KQRURM/00wwQdVHUv/3z+\n"
  "r7VdZm2wYVd9jAnb0xt+TVsdBoDEmJ1ZAtdNl4m2KN3YYbnlqCz0xvRjDGaEQAVv\n"
  "nzaDSPXp1RtBwSlfmXyH98O6LVbqHlL1VmmzVuaaDCemLAhHEVkbWf2QNZycUFCQ\n"
  "vCkfEYA1+e/NxSyKnu6tv6BkRyR5VS93wZIDlfi8JdLhFp39VVLwiSPYEDEyc6VJ\n"
  "IW0TW6LAlSxAXH2JGSV59RMeqouw47jGdVs2T0AnpNpPyVcSHNkxOiQh9N9pQi/s\n"
  "ObXxeWEyfkzUEo79aYjjoJG64yXoWPo2lA==\n"
  "-----END CERTIFICATE-----\n"
  "";

static const char WSS_SERVER_KEY_PEM[] =
  "-----BEGIN PRIVATE KEY-----\n"
  "MIIEvAIBADANBgkqhkiG9w0BAQEFAASCBKYwggSiAgEAAoIBAQC60piC7JID9Bzp\n"
  "XLJbnm+FyR7f/HonUcFWvJhxg9SloybuO7RTDGTPPkrMWrKrHBKiBJHckTwmyWz7\n"
  "q61BRTpC7+mxl4xB47cA355uLrHKhnmuIO4kOSWPMyLMNqXJhLW6Cdc++FzG89xj\n"
  "3/6pGmNDPpffvPFgvKDG1ZuO96k47tiF8sGRUCw5frvhrxVMDlERO06TdJ2m3wwZ\n"
  "8zmaFiFZPkJGPuah1euFPznpPbPnxmj0Y0k3rCpSpOL+eh5MfhVk318ZEGB7KeY1\n"
  "6tOEJwjhvcyjc6IXi+W0jR3/A4mZOCbYFyB906Ma2yjfOMixhS0WUYUOI/9VQ7IC\n"
  "BVfooDYnAgMBAAECggEAHKlrkYaG3klo2BqLGIhFltJkxLtg6EBgN5TmlF/D04ek\n"
  "1N7fCvC3VJ47bn+pspuzdhzaXesAPfkgGbpcccuTpSpWgZL9ByzO6jjkfwX169rW\n"
  "tzLBJliIHSCcwZTOuYx9LuYMmlITNci68q2uTbZJLUtglIhRxeQajHbAD16KnTuw\n"
  "VCc3XmfJEoUX/QHS09GfgaWyiYxUa2vJrlZJdueZ+trV9B943B42gIzKf7bNhG1C\n"
  "kU+ftSfDbUOTWJVl7j1cielvcUoRage23gRkR8/F/D64Bfx/BTv/NMye9GYck+/V\n"
  "3FhEZXau5moBkj+W805rFUemHne9GMqmmGiyW9EzOQKBgQDmvLolUuVew5L32Z3a\n"
  "pByoRGloKaboQzZPpP+TAT+YuRRC4xeWu/R5euKKUx1a1oXOrSulq/g3AHOHX2QN\n"
  "O+hFxK2AZjvVHAxiyeWzK5jN01X+Ag48Tag5QRVPKnY8n0iE2sq//thkj6MtloTo\n"
  "cA5IuXzaJ1WVFetSYLdyH9przwKBgQDPRv+dYu/6Z5hD8jnyI4i5r805JLFBphuQ\n"
  "UQ1fJd26vAX5jsiFNiU1HiaUWwKqvb7DYjBK9X6/Q86hEBo3ays5JJTHHaCMIgSN\n"
  "HeX6zFOSu0g3NQ7azXdZroec5nmYIvtWXW/M0+SCykneU/2YDOCpLEUu8Mptv2Iq\n"
  "kT5YwQ5uKQKBgClyJQ2nQkQvZSrYEB7B89PV1gk70htOV2cfADlrs/y+4gDRwd71\n"
  "jAh2JlMqcs9qUUa6nGh553AZyKa8X8BwzlwZ5AB4XK7/BsqH8Zapch0mpP+nZt82\n"
  "I82E/0rlGa/x/JjApEKwiDYoBuudaepGlopBp0eYYSsScN6vOMRrLFTJAoGAIZD4\n"
  "0g70l9Nw6Afw9FJm0NM0Y72aauwwaIWHqdHi/1t1iD1kIZZsHhb83meCng3ON6vN\n"
  "8EWjOewMKS8HX9QeEsnmwfv9RNQ5cnTWCnBOVGO5xR71gd9mJYLK+VClfkqj8w98\n"
  "jGoABmIl5jgpyBbFxv+hUIGz9mPYzNXqh8rzwEkCgYAQ6viKrsEgWcQjs6e90+Tz\n"
  "6uIfwo1HJCufci7TKqbo/mJA8LBLAWOxM7Lwlb7QCma6U1yYoVX4fTJ3kt7WJhN0\n"
  "AkoMNxMUz6mRdT3WJJ1539g8OdWGRDn3oulnNIA+CKl17nWLCGFKXFfAwrgbPvWA\n"
  "uH2cOqY+aTR6zOjr+OhiLw==\n"
  "-----END PRIVATE KEY-----\n"
  "";

#endif
