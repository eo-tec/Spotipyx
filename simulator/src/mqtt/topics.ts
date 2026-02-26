// MQTT topic helpers for Spotipyx protocol

export function commandTopic(pixieId: number): string {
  return `pixie/${pixieId}`;
}

export function responseTopic(pixieId: number): string {
  return `pixie/${pixieId}/response/#`;
}

export function requestTopic(pixieId: number, type: string): string {
  return `pixie/${pixieId}/request/${type}`;
}

export function responseTopicFor(pixieId: number, type: string): string {
  return `pixie/${pixieId}/response/${type}`;
}
