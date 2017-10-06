cd /genomics/scratch
for file in *.sam; do 
	bamname=$(echo $file | awk -F. '{print $1}'); 
	bamname="$bamname""$(hostname)"".bam" ; 
	/genomics/samtools/samtools sort $file -o $bamname -@1 ; 
done

