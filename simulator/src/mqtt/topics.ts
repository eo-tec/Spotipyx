// MQTT topic helpers for Spotipyx protocol

export function commandTopic(pixieId: number): string {
  return `frame/${pixieId}`;
}

export function responseTopic(pixieId: number): string {
  return `frame/${pixieId}/response/#`;
}

export function requestTopic(pixieId: number, type: string): string {
  return `pixie/${pixieId}/request/${type}`;
}

export function responseTopicFor(pixieId: number, type: string): string {
  return `frame/${pixieId}/response/${type}`;
}
