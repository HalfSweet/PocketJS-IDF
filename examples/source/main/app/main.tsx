import { mount } from "@pocketjs/framework";
import { Text, View } from "@pocketjs/framework/components";
import { batch, createSignal, onMount } from "solid-js";

function App() {
  const [status, setStatus] = createSignal("booting");
  onMount(() => {
    batch(() => setStatus("reactive"));
  });

  return (
    <View class="w-[320] h-[180] flex-col items-center justify-center bg-slate-950">
      <Text class="text-xl font-bold text-white">PocketJS-IDF</Text>
      <Text class="text-sm text-cyan-400">esp32p4-idf ABI 1</Text>
      <Text class="text-sm text-emerald-400">Solid: {status()}</Text>
    </View>
  );
}

mount(() => <App />);
