import { mount } from "@pocketjs/framework";
import { Text, View } from "@pocketjs/framework/components";

function App() {
  return (
    <View class="w-[320] h-[180] flex-col items-center justify-center bg-slate-950">
      <Text class="text-xl font-bold text-white">PocketJS-IDF</Text>
      <Text class="text-sm text-cyan-400">esp32p4-idf ABI 1</Text>
    </View>
  );
}

mount(() => <App />);
