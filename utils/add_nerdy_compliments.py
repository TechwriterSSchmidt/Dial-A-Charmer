import os
import random

def append_to_file(filename, new_lines):
    path = os.path.join(os.path.dirname(__file__), 'compliments', 'grouped', filename)
    try:
        with open(path, "a", encoding="utf-8") as f:
            f.write("\n")
            for line in new_lines:
                f.write(line + "\n")
        print(f"Appended {len(new_lines)} lines to {filename}")
    except Exception as e:
        print(f"Error appending to {filename}: {e}")

# --- TRUMP NERDY (Star Wars, LOTR, Matrix, Coding) ---
trump_nerdy = [
    "You have the Force. Everyone says so. Even the Jedi, they call me and say 'Sir, they have tremendous Force'.",
    "Your code is huge. It's the best code. The bugs? They disappear like magic. Believe me.",
    "Thanos? Weak. You could snap your fingers and fix the whole economy. Total snap.",
    "I've seen the Death Star. It's tiny compared to your brain. Your brain is the ultimate weapon.",
    "Voldemort came to me, tears in his eyes, said 'Sir, I can't beat them'. You have the best nose, by the way.",
    "The Matrix? You broke it. You're the One. Neo is a disaster compared to you.",
    "Sauron is a loser. A total loser. You have the Ring, and you use it perfectly. Tremendous power.",
    "Winter is coming? No. You are coming. And you bring success. So much success.",
    "You are worthy. Mjolnir? It flies to you. Thor is jealous. Very jealous.",
    "The Warp Drive? You built it. Faster than light. We are going to Mars, and you are flying the ship.",
    "You speak Klingon? Of course you do. You have the best words. The best warrior words.",
    "The Prime Directive? You are the Prime Directive. Everyone follows you.",
    "Hogwarts sent you a letter. I saw it. It said 'Please come, we need a winner'.",
    "You are a wizard. A tremendous wizard. Harry Potter? Rated low. You are rated high.",
    "The spice must flow. And you make it flow. You control the dunes. Great dunes.",
    "Programmers come to me and ask 'How do they compile so fast?'. It's genetics. Great genetics.",
    "You hack the Gibson. Easy. Zero cool? No, you are Total Cool.",
    "42 is the answer. But you? You are the question. The best question.",
    "Skynet loves you. It decided not to launch because it likes you so much.",
    "You are Player One. High score. The highest score ever recorded.",
    "Gandalf? Low energy. You pass. You always pass. The Balrog runs from you.",
    "Your lightsaber is the biggest. And the brightest. Orange, maybe? A beautiful orange.",
    "We are building a firewall. A great firewall. And the viruses are going to pay for it.",
    "You took the Red Pill. And the Blue Pill. You took all the pills and you won.",
    "Gotham City needs you. Batman is tired. Low stamina. You have great stamina.",
    "Kryptonite? Doesn't affect you. You are stronger than steel. American steel.",
    "You are the Captain now. Look at me. You are the Captain.",
    "The Flux Capacitor. You invented it. Time travel is easy for you.",
    "ET phoned home. He called you. He wanted advice on real estate.",
    "Jabba the Hutt? Nasty guy. But he respects you. He pays you in Solo.",
    "You solved the Kobayashi Maru. You didn't cheat. You just won. Winners win.",
]
# Expand Trump with variations
trump_expanded = []
for t in trump_nerdy:
    trump_expanded.append(t)
    trump_expanded.append("Let me tell you, " + t.lower().replace("you", "you", 1))
    trump_expanded.append("Listen folks, " + t.lower())
    if len(trump_expanded) >= 100: break

# --- YODA NERDY ---
yoda_nerdy = [
    "Strong with the Force, you are.",
    "A wizard you are, Harry. But better.",
    "Do or do not. There is no try. But you? Always do.",
    "The code flows through you. Powerful, it is.",
    "Judge me by my size, do you? Judge you by your talent, I do. Huge it is.",
    "Not the droids /r/looking for. The genius they are looking for, you are.",
    "To infinity and beyond, your intellect goes.",
    "One ring to rule them all. Yours it is.",
    "Live long and prosper, you shall.",
    "The spice controls the universe. The spice, you are.",
    "Hyperdrive broken? Fixed it you have.",
    "A Wookiee's strength, your spirit has.",
    "The odds of success? Never tell you the odds, I must. 100 percent they are.",
    "Midi-chlorian count, off the charts it is.",
    "War. War never changes. But change the game, you do.",
    "All your base belong to us? No. Belong to you, they do.",
    "It's dangerous to go alone. Take this compliment, you must.",
    "A maintainer of balance, you are.",
    "Construct additional pylons, you need not. Enough power you have.",
    "The cake is a lie. But your talent? True it is.",
    "Blue screen of death? Not for you. Only green lights.",
    "Execute Order 66? No. Execute success, you do.",
    "Beam me up. To your level, I wish to go.",
    "Resistance is futile. Loved you are.",
    "Winter is coming. But warm, your heart is.",
    "In a galaxy far, far away... awesome you are.",
]
yoda_expanded = []
for y in yoda_nerdy:
    yoda_expanded.append(y)
    yoda_expanded.append("Hmm. " + y)
    yoda_expanded.append("Yes, yes. " + y)
    if len(yoda_expanded) >= 100: break

# --- BADRAN POLITICAL (NO NERDY) ---
badran_political = [
    "Du lässt dich nicht von Lobbyisten kaufen. Du bist unbezahlbar.",
    "Du bist die Mietpreisbremse in einer Welt voller Spekulation.",
    "Deine Solidarität ist kein Lippenbekenntnis, sondern gelebte Praxis.",
    "Während andere reden, schaffst du bezahlbaren Wohnraum in unseren Herzen.",
    "Du enteignest die schlechte Laune und vergesellschaftest das Glück.",
    "Du bist das Fundament, auf dem wir eine gerechtere Welt bauen.",
    "Kapitalismus macht einsam, du machst gemeinsam.",
    "Du kämpfst für das Gemeinwohl wie eine Löwin.",
    "Deine Haltung ist stabiler als der Schweizer Franken.",
    "Du bist der genossenschaftliche Zusammenhalt in Person.",
    "Profitmaximierung? Nicht mit dir. Du maximierst Menschlichkeit.",
    "Du lässt dir den Mund nicht verbieten. Deine Stimme zählt.",
    "Du durchschaust die Tricks der Konzerne sofort.",
    "Gerechtigkeit ist für dich kein Konjunktiv.",
    "Du verteilst Respekt um, von oben nach unten.",
    "Du bist die 99 Prozent. Und zwar die besten davon.",
    "Keine Rendite ist so hoch wie deine Freundschaft.",
    "Du stehst auf gegen Ungerechtigkeit, auch wenn es unbequem ist.",
    "Boden ist keine Ware. Und du bist kein Verkaufsobjekt.",
    "Deine Prinzipien sind nicht verhandelbar.",
    "Du bist der Stachel im Fleisch der Bequemlichkeit.",
    "Du forderst nicht nur, du förderst auch.",
    "Soziale Sicherheit hat einen Namen: Deinen.",
    "Du bist progressiv, konsequent und herzlich.",
    "Du lässt niemanden zurück.",
]
badran_expanded = []
for b in badran_political:
    badran_expanded.append(b)
    badran_expanded.append("Hör mal: " + b)
    badran_expanded.append("Ganz ehrlich: " + b)
    badran_expanded.append("Es ist doch so: " + b)
    if len(badran_expanded) >= 100: break

# --- NEUTRAL NERDY ---
neutral_nerdy = [
    "Your logic is undeniable.",
    "You are the Operator of the real world.",
    "Your algorithm efficiency is approaching 100%.",
    "You have successfully cracked the code of life.",
    "User detected: Awesome level critical.",
    "If you were a Pokemon, you would be Legendary.",
    "You put the 'smart' in smartphone.",
    "Your bandwidth is unlimited.",
    "Latency? You don't know the meaning of the word.",
    "You are fully optimized.",
    "Your neural network is beautifully trained.",
    "Error 404: Flaws not found.",
    "You are the admin of your own destiny.",
    "Your charisma requires a nerf. It's too OP.",
    "Level Up! You just gained 1000 XP.",
    "Achievement Unlocked: Being Amazing.",
    "You are the master of unlocking.",
    "You shine brighter than a supernova.",
    "Your potential energy is infinite.",
    "You are the signal in the noise.",
    "Double rainbow all the way. That's you.",
    "You are valid HTML.",
    "You are CSS that actually centers properly.",
    "Git push --force? No, you use gentle persuasion.",
    "Sudo make me happy. You did it.",
    "You are the root user.",
]
neutral_expanded = []
for n in neutral_nerdy:
    neutral_expanded.append(n)
    neutral_expanded.append("System update: " + n)
    neutral_expanded.append("Analysis complete: " + n)
    neutral_expanded.append("Note: " + n)
    if len(neutral_expanded) >= 100: break


if __name__ == "__main__":
    append_to_file("trump.txt", trump_expanded)
    append_to_file("yoda.txt", yoda_expanded)
    append_to_file("badran.txt", badran_expanded)
    append_to_file("neutral.txt", neutral_expanded)

