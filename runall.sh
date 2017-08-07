make
rsync -avz /home/odroid/sorting_framework/ odroid@odroid03.cse.unsw.edu.au:/home/odroid/sorting_framework/
rsync -avz /home/odroid/sorting_framework/ odroid@odroid04.cse.unsw.edu.au:/home/odroid/sorting_framework/
rsync -avz /home/odroid/sorting_framework/ odroid@odroid05.cse.unsw.edu.au:/home/odroid/sorting_framework/
./main & 
ansible-playbook ansible.yml
